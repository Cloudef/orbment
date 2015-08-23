#include <orbment/plugin.h>
#include <chck/string/string.h>
#include <chck/math/math.h>
#include <chck/lut/lut.h>
#include <assert.h>
#include "config.h"

static bool (*add_hook)(plugin_h, const char *name, const struct function*);
static bool (*add_node)(plugin_h, const char *container, const char *path, const char *type, void *arg, const struct function *read, const struct function *write, const struct function *clunk, const struct function *size);
static bool (*add_fs)(plugin_h, const char *name);
static bool (*reply)(uint16_t tag, const void *src, size_t size, size_t nmemb, void *stream);

static const char *load_sig = "*(c[],sz*)|1";
static const char *save_sig = "b(c[],*,sz)|1";
static const char *pair_sig = "c[],c[]|1";

struct pair {
   char *key, *value;
};

struct configuration_backend {
   plugin_h handle;
   const char *name;

   struct pair* (*load)(const char *stsign, size_t *out_memb);
   bool (*save)(const char *stsign, const struct pair*, size_t memb);
};

static struct {
   plugin_h self;
   struct chck_hash_table table;
   struct configuration_backend backend;
} plugin;

static bool
cb_key_read(void *arg, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, void *stream)
{
   (void)fid;
   const char *key = arg;
   const char *data = chck_hash_table_str_get(&plugin.table, key, strlen(key));
   data = (data ? *(const char**)data : NULL);
   const size_t len = (data ? strlen(data) : 0);
   offset = chck_minsz(len, offset);
   count = chck_minsz(len - offset, count);
   return (reply ? reply(tag, data + offset, 1, count, stream) : false);
}

PPURE static bool
validate_key(const char *key)
{
   if (chck_cstr_is_empty(key))
      return false;

   /* A leading slash is required */
   if (key[0] != '/')
      return false;

   for (uint32_t i = 1; key[i]; i++) {
      /* a-zA-Z0-9_- only */
      if (key[i] >= 'a' && key[i] <= 'z')
         continue;
      if (key[i] >= 'A' && key[i] <= 'Z')
         continue;
      if (key[i] >= '0' && key[i] <= '9')
         continue;
      if (key[i] == '-' || key[i] == '_')
         continue;

      /* Slashes cannot be adjacent, or at the end of the key */
      if (key[i] == '/' && key[i - 1] != '/' && key[i + 1] != '\0')
         continue;

      return false;
   }

   return true;
}

static void
free_ptr(void **ptr)
{
   free((ptr ? *ptr : NULL));
}

static void
load_config(void)
{
   chck_hash_table_for_each_call(&plugin.table, free_ptr);
   chck_hash_table_release(&plugin.table);

   if (!plugin.backend.load)
      return;

   if (!chck_hash_table(&plugin.table, 0, 256, sizeof(char*)))
      return;

   size_t memb;
   struct pair *pairs;
   if (!(pairs = plugin.backend.load(pair_sig, &memb)))
      return;

   for (size_t i = 0; i < memb; ++i) {
      if (!validate_key(pairs[i].key)) {
         plog(plugin.self, PLOG_WARN, "Failed to validate key: %s", pairs[i].key);
         free(pairs[i].key);
         free(pairs[i].value);
         continue;
      }

      plog(plugin.self, PLOG_INFO, "%s = %s", pairs[i].key, pairs[i].value);
      chck_hash_table_str_set(&plugin.table, pairs[i].key, strlen(pairs[i].key), &pairs[i].value);
      free(pairs[i].key);
   }

   free(pairs);

   if (add_node) {
      void *v;
      chck_hash_table_for_each(&plugin.table, v)
         add_node(plugin.self, "configuration", _I.str_key + 1, "file", (void*)_I.str_key, FUN(cb_key_read, "b(*,u16,u32,u64,u32,*)|1"), NULL, NULL, NULL);
   }
}

static bool
save_config(void)
{
   if (!plugin.backend.save)
      return false;

   // XXX: implement
   return false;
}

static bool
add_configuration_backend(plugin_h caller, const char *name, struct function *load, const struct function *save)
{
   if (plugin.backend.name) {
      plog(plugin.self, PLOG_WARN, "Configuration backend '%s' already loaded.", plugin.backend.name);
      return false;
   }

   if (!load || !save)
      return false;

   if (chck_cstr_is_empty(name)) {
      plog(plugin.self, PLOG_ERROR, "Configuration backend must have a nonempty name.");
      return false;
   }

   if (!chck_cstreq(load->signature, load_sig)) {
      plog(plugin.self, PLOG_ERROR, "Wrong signature provided for configuration backend load() (%s != %s)", load->signature, load_sig);
      return false;
   }

   if (!chck_cstreq(save->signature, save_sig)) {
      plog(plugin.self, PLOG_ERROR, "Wrong signature provided for configuration backend save() (%s != %s)", save->signature, save_sig);
      return false;
   }

   plugin.backend.load = load->function;
   plugin.backend.save = save->function;
   plugin.backend.name = name;
   plugin.backend.handle = caller;
   load_config();
   return true;
}

static bool
get(const char *key, char type, void *value_out)
{
   if (!validate_key(key)) {
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': invalid key format.", key);
      return false;
   }

   if (!type || !strchr("uidsb", type)) { /* Integer, Unsigned Integer, Double, String, Boolean */
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': invalid type character '%c'.", key, type);
      return false;
   }

   const char *data = chck_hash_table_str_get(&plugin.table, key, strlen(key));
   data = (data ? *(const char**)data : NULL);

   if (chck_cstr_is_empty(data))
      return false;

   switch (type) {
      case 's':
         *(const char**)value_out = data;
         return true;
         break;

      case 'u': return chck_cstr_to_u32(data, value_out);
      case 'i': return chck_cstr_to_i32(data, value_out);
      case 'd': return chck_cstr_to_d(data, value_out);
      case 'b': return chck_cstr_to_bool(data, value_out);

      default:
         assert(false && "there should always be a valid type");
   }

   return false;
}

static void
plugin_deloaded(plugin_h ph)
{
   if (ph != plugin.backend.handle)
      return;

   memset(&plugin.backend, 0, sizeof(plugin.backend));
}

static bool
vfs_init(plugin_h self)
{
   plugin_h vfs;
   if (!(vfs = import_plugin(self, "vfs")))
      return false;

   if (!(add_fs = import_method(self, vfs, "add_fs", "b(h,c[])|1")) ||
       !(add_node = import_method(self, vfs, "add_node", "b(h,c[],c[],c[],*,fun,fun,fun,fun)|1")) ||
       !(reply = import_method(self, vfs, "reply", "b(u16,*,sz,sz,*)|1")))
      goto fail;

   return add_fs(self, "configuration");

fail:
   add_fs = NULL;
   add_node = NULL;
   reply = NULL;
   return false;
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   save_config();

   chck_hash_table_for_each_call(&plugin.table, free_ptr);
   chck_hash_table_release(&plugin.table);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   vfs_init(self);

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   return (add_hook(self, "plugin.deloaded", FUN(plugin_deloaded, "v(h)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_configuration_backend, "b(h,c[],fun,fun)|1"),
      REGISTER_METHOD(get, "b(c[],c,v)|1"),
      {0}
   };

   static const char *groups[] = {
      "configuration",
      NULL,
   };

   static const char *after[] = {
      "vfs",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "configuration",
      .description = "Configuration api.",
      .version = VERSION,
      .methods = methods,
      .groups = groups,
      .after = after,
   };

   return &info;
}
