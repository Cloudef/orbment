#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <wlc/wlc.h>
#include <orbment/plugin.h>
#include <chck/string/string.h>
#include <chck/math/math.h>
#include <chck/fs/fs.h>
#include "config.h"
#include "fs.h"

static bool (*add_hook)(plugin_h, const char *name, const struct function*);

struct connection {
   struct wlc_event_source *source;
   int fd;
};

struct cnode {
   size_t node;
   plugin_h owner;
};

struct container {
   struct chck_iter_pool nodes;
   size_t root;
   plugin_h owner;
};

static struct {
   // if we ever allow multiple clients, the pi9 struct should be per-client
   struct chck_hash_table containers;
   struct pi9 pi9;
   struct fs fs;
   struct chck_string path;
   struct connection client;
   struct connection server;
   size_t plugins, outputs;
   plugin_h self;
} plugin;

static size_t
add_file(size_t parent, const char *name, struct node_procs *procs, void *arg)
{
   struct node n;
   node_init(&n, procs);
   n.userdata = arg;
   n.stat = (struct pi9_stat) {
      .type = 0,
      .dev = 0,
      .qid = { PI9_QTFILE, 0, 1 },
      .mode = 0600,
      .atime = time(NULL),
      .mtime = time(NULL),
      .length = 0,
      .name = { NULL, 0, false },
      .uid = { NULL, 0, false },
      .gid = { NULL, 0, false },
      .muid = { NULL, 0, false },
   };

   if (!pi9_string_set_cstr(&n.stat.name, name, true))
      return NONODE;

   size_t node;
   if ((node = fs_add_node_linked(&plugin.fs, &n, parent)) == NONODE) {
      node_release(&n);
      return NONODE;
   }

   return true;
}

static size_t
add_dir(size_t parent, const char *name, void *arg)
{
   struct node n;
   node_init(&n, NULL);
   n.userdata = arg;
   n.stat = (struct pi9_stat) {
      .type = 0,
      .dev = 0,
      .qid = { PI9_QTDIR, 0, 1 },
      .mode = PI9_DMDIR | 0700,
      .atime = time(NULL),
      .mtime = time(NULL),
      .length = 0,
      .name = { NULL, 0, false },
      .uid = { NULL, 0, false },
      .gid = { NULL, 0, false },
      .muid = { NULL, 0, false },
   };

   if (!pi9_string_set_cstr(&n.stat.name, name, true))
      return NONODE;

   size_t node;
   if ((node = fs_add_node_linked(&plugin.fs, &n, parent)) == NONODE) {
      node_release(&n);
      return NONODE;
   }

   return node;
}

enum type {
   TFILE,
   TDIR,
   LAST
};

static enum type
type_for_string(const char *type)
{
   struct {
      const char *name;
      enum type type;
   } map[] = {
      { "file", TFILE },
      { "dir", TDIR },
      { NULL, LAST },
   };

   for (uint32_t i = 0; map[i].name; ++i) {
      if (chck_cstreq(type, map[i].name))
         return map[i].type;
   }

   return LAST;
}

static size_t
find_node(struct container *c, const char *name)
{
   assert(c);

   struct cnode *cn;
   chck_iter_pool_for_each(&c->nodes, cn) {
      struct node *n;
      if (!(n = get_node(&plugin.fs, cn->node)) || !pi9_string_eq_cstr(&n->stat.name, name))
         continue;

      return cn->node;
   }
   return NONODE;
}

static size_t
add_node_of_type(plugin_h caller, struct container *c, size_t parent, const char *name, enum type t, void *arg, struct node_procs *procs)
{
   size_t ret = NONODE;
   switch (t) {
      case TFILE:
         ret = add_file(parent, name, procs, arg);
      break;

      case TDIR:
         ret = add_dir(parent, name, arg);
      break;

      case LAST:
      break;
   }

   if (ret == NONODE)
      return false;

   if (!c->nodes.items.member && !chck_iter_pool(&c->nodes, 4, 0, sizeof(struct cnode)))
      goto fail;

   struct cnode cnode = {
      .node = ret,
      .owner = caller,
   };

   if (!chck_iter_pool_push_back(&c->nodes, &cnode))
      goto fail;

   return ret;

fail:
   fs_remove_node(&plugin.fs, get_node(&plugin.fs, ret));
   return NONODE;
}

static size_t
create_subnodes(plugin_h caller, struct container *c, const char *path, size_t len)
{
   assert(c && path && len);

   struct chck_string tmp = {0};
   if (!chck_string_set_cstr_with_length(&tmp, path, len, true))
      return NONODE;

   size_t index = NONODE, last = c->root;
   for (char *s = strchr(tmp.data, '/'); s - tmp.data < (ptrdiff_t)len &&  s && *s; s = strchr(tmp.data, '/')) {
      *s = 0;
      if ((index = find_node(c, tmp.data)) == NONODE && (index = add_node_of_type(caller, c, last, tmp.data, TDIR, NULL, NULL)) == NONODE)
         goto out;

      last = index;
      ++s;
   }

out:
   chck_string_release(&tmp);
   return index;
}

static bool
reply(uint16_t tag, const void *src, size_t size, size_t nmemb, struct pi9_stream *stream)
{
   struct pi9_reply r;
   pi9_reply_start(&r, tag, stream);

   if (pi9_write(src, size, nmemb, stream) != nmemb)
      return false;

   return pi9_reply_send(&r, plugin.client.fd, stream);
}

static bool
add_node(plugin_h caller, const char *container, const char *path, const char *type, void *arg, const struct function *read, const struct function *write, const struct function *clunk, const struct function *size)
{
   const char *name;
   struct container *c;
   if (!caller || !container || !path || !(name = chck_basename(path)) || !(c = chck_hash_table_str_get(&plugin.containers, container, strlen(container))))
      return false;

   struct {
      const struct function *f;
      const char *signature;
   } map[] = {
      { read, "b(*,u16,u32,u64,u32,*)|1" },
      { write, "b(*,u16,u32,u64,u32,*)|1" },
      { clunk, "v(*,u32)|1" },
      { size, "u64(*)|1" },
      { NULL, NULL }
   };

   for (uint32_t i = 0; map[i].signature; ++i) {
      if (map[i].f && !chck_cstreq(map[i].f->signature, map[i].signature)) {
         plog(plugin.self, PLOG_WARN, "Wrong signature provided for '%s' function. (%s != %s)", name, map[i].signature, map[i].f->signature);
         return false;
      }
   }

   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      plog(plugin.self, PLOG_WARN, "Invalid type for node '%s'. (%s)", name, type);
      return false;
   }

   size_t parent = (name - path > 0 ? create_subnodes(caller, c, path, name - path) : c->root);
   if (parent == NONODE)
      return false;

   struct node_procs procs = {
      .read = (read ? read->function : NULL),
      .write = (write ? write->function : NULL),
      .clunk = (clunk ? clunk->function : NULL),
      .size = (size ? size->function : NULL),
   };

   if (add_node_of_type(caller, c, parent, name, t, arg, &procs) == NONODE)
      return false;

   plog(plugin.self, PLOG_INFO, "Added node: %s", name);
   return true;
}

static bool
add_fs_to(size_t parent, plugin_h owner, const char *name)
{
   if (!owner)
      return false;

   if (!plugin.containers.lut.table && !chck_hash_table(&plugin.containers, 0, 256, sizeof(struct container)))
      return false;

   if (chck_hash_table_str_get(&plugin.containers, name, strlen(name))) {
      plog(plugin.self, PLOG_WARN, "Container already exists for name '%s'", name);
      return false;
   }

   struct container container;
   memset(&container, 0, sizeof(container));
   container.owner = owner;

   if ((container.root = add_dir(parent, name, NULL)) == NONODE)
      return false;

   if (!chck_hash_table_str_set(&plugin.containers, name, strlen(name), &container))
      goto error0;

   plog(plugin.self, PLOG_INFO, "Added fs: %s", name);
   return true;

error0:
   fs_remove_node(&plugin.fs, get_node(&plugin.fs, container.root));
   return false;
}

static bool
add_fs(plugin_h caller, const char *name)
{
   return add_fs_to(plugin.plugins, caller, name);
}

static void
cnode_release(struct cnode *cn)
{
   if (!cn)
      return;

   fs_remove_node(&plugin.fs, get_node(&plugin.fs, cn->node));
}

static void
container_release(struct container *c)
{
   if (!c)
      return;

   chck_iter_pool_for_each_call(&c->nodes, cnode_release);
   chck_iter_pool_release(&c->nodes);
   fs_remove_node(&plugin.fs, get_node(&plugin.fs, c->root));
}

static void
remove_fs(plugin_h caller, const char *name)
{
   if (!caller)
      return;

   struct container *c;
   if (!(c = chck_hash_table_str_get(&plugin.containers, name, strlen(name))) || c->owner != caller)
      return;

   container_release(c);
   chck_hash_table_str_set(&plugin.containers, name, strlen(name), NULL);
   plog(plugin.self, PLOG_INFO, "Removed fs: %s", name);
}

static void
remove_fses_for_plugin(plugin_h caller)
{
   struct container *c;
   chck_hash_table_for_each(&plugin.containers, c) {
      if (c->owner != caller) {
         struct cnode *cn;
         chck_iter_pool_for_each(&c->nodes, cn) {
            if (cn->owner != caller)
               continue;

            cnode_release(cn);
            chck_iter_pool_remove(&c->nodes, _I - 1);
            --_I;
         }
         continue;
      }

      container_release(c);
      memset(c, 0, sizeof(struct container));
   }
}

static int
cb_process(int fd, uint32_t mask, void *arg)
{
   (void)mask, (void)arg;

   if (!plugin.client.source)
      return 0;

   char buffer[32];
   if (recv(fd, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0) {
      wlc_event_source_remove(plugin.client.source);
      close(fd);
      plugin.client.source = NULL;
      plugin.client.fd = -1;
      plog(plugin.self, PLOG_INFO, "Client disconnected");
   } else {
      pi9_process(&plugin.pi9, fd);
   }

   return 0;
}

static int
cb_mount(int fd, uint32_t mask, void *arg)
{
   (void)mask, (void)arg;

   int cfd;
   if ((cfd = accept(fd, NULL, NULL)) < 0)
      return 0;

   if (plugin.client.source) {
      plog(plugin.self, PLOG_WARN, "Only single client allowed at time");
      close(cfd);
      return 0;
   }

   if (!(plugin.client.source = wlc_event_loop_add_fd(cfd, WLC_EVENT_READABLE, cb_process, NULL))) {
      close(cfd);
      return 0;
   }

   plugin.client.fd = cfd;
   plog(plugin.self, PLOG_INFO, "Client connected");
   return 0;
}

static bool
output_created(wlc_handle output)
{
   return add_fs_to(plugin.outputs, plugin.self, wlc_output_get_name(output));
}

static void
output_destroyed(wlc_handle output)
{
   remove_fs(plugin.self, wlc_output_get_name(output));
}

static void
plugin_deloaded(plugin_h ph)
{
   remove_fses_for_plugin(ph);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;

   chck_hash_table_for_each_call(&plugin.containers, container_release);
   chck_hash_table_release(&plugin.containers);

   struct connection *c[] = { &plugin.client, &plugin.server, NULL };
   for (uint32_t i = 0; c[i]; ++i) {
      if (c[i]->source)
         wlc_event_source_remove(c[i]->source);
      if (c[i]->fd >= 0)
         close(c[i]->fd);
   }

   pi9_release(&plugin.pi9);
   fs_release(&plugin.fs);

   if (plugin.path.data)
      unlink(plugin.path.data);

   chck_string_release(&plugin.path);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;
   plugin.client.fd = plugin.server.fd = -1;

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   const char *xrd;
   if (!(xrd = getenv("XDG_RUNTIME_DIR")) || chck_cstr_is_empty(xrd)) {
      plog(plugin.self, PLOG_ERROR, "No XDG_RUNTIME_DIR set");
      return false;
   }

   if (!fs_init(&plugin.fs, 3))
      return false;

   if (!pi9_init(&plugin.pi9, 0, &pi9_procs, &plugin.fs))
      return false;

   if (!chck_string_set_format(&plugin.path, "%s/%s", xrd, "orbment.9p"))
      return false;

   if ((plugin.server.fd = announce_unix(plugin.path.data)) < 0)
      return false;


   if (!(plugin.server.source = wlc_event_loop_add_fd(plugin.server.fd, WLC_EVENT_READABLE, cb_mount, NULL))) {
      close(plugin.server.fd);
      return false;
   }

   if ((plugin.plugins = add_dir(plugin.fs.root, "plugins", NULL)) == NONODE ||
       (plugin.outputs = add_dir(plugin.fs.root, "outputs", NULL)) == NONODE)
      return false;

   plog(plugin.self, PLOG_INFO, "9p mountpoint at %s", plugin.path.data);
   return (add_hook(self, "plugin.deloaded", FUN(plugin_deloaded, "v(h)|1")) &&
           add_hook(self, "output.created", FUN(output_created, "b(h)|1")) &&
           add_hook(self, "output.destroyed", FUN(output_destroyed, "v(h)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_fs, "b(h,c[])|1"),
      REGISTER_METHOD(remove_fs, "v(h,c[])|1"),
      REGISTER_METHOD(add_node, "b(h,c[],c[],c[],*,fun,fun,fun,fun)|1"),
      REGISTER_METHOD(reply, "b(u16,*,sz,sz,*)|1"),
      {0},
   };

   static const struct plugin_info info = {
      .name = "vfs",
      .description = "Virtual file system api.",
      .version = VERSION,
      .methods = methods,
   };

   return &info;
}
