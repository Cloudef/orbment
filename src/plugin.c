#include "plugin.h"
#include <wlc.h>
#include <stdio.h>
#include <assert.h>
#include "chck/dl/dl.h"
#include "chck/lut/lut.h"
#include "chck/pool/pool.h"

static const size_t NOTINDEX = (size_t)-1;

static struct chck_pool plugins;
static struct chck_hash_table names;
static struct chck_hash_table groups;

static plugin_h
get_handle(const char *name)
{
   assert(name);
   plugin_h *h = chck_hash_table_str_get(&names, name, strlen(name));
   return (h ? *h : NOTINDEX);
}

static struct plugin*
get(const char *name)
{
   assert(name);
   return chck_pool_get(&plugins, get_handle(name));
}

static void
unlink_name(const char *name)
{
   assert(name);
   chck_hash_table_str_set(&names, name, strlen(name), &NOTINDEX);
}

static bool
link_name(const char *name, plugin_h handle)
{
   assert(name);

   if (handle == NOTINDEX || (!names.lut.table && !chck_hash_table(&names, NOTINDEX, 256, sizeof(plugin_h))))
      return false;

   return chck_hash_table_str_set(&names, name, strlen(name), &handle);
}

static struct chck_iter_pool*
get_group(const char *name)
{
   assert(name);
   return (groups.lut.table ? chck_hash_table_str_get(&groups, name, strlen(name)) : NULL);
}

static void
remove_from_group(const char *name, plugin_h handle)
{
   assert(name);

   struct chck_iter_pool *pool;
   if (!(pool = get_group(name)))
      return;

   plugin_h *h;
   chck_iter_pool_for_each(pool, h) {
      if (*h != handle)
         continue;

      chck_iter_pool_remove(pool, _I - 1);
      break;
   }
}

static bool
exists_in_pool(struct chck_iter_pool *pool, plugin_h handle)
{
   assert(pool);
   plugin_h *h;
   chck_iter_pool_for_each(pool, h) {
      if (*h == handle)
         return true;
   }
   return false;
}

static bool
add_to_group(const char *name, plugin_h handle)
{
   assert(name);

   if (handle == NOTINDEX || (!groups.lut.table && !chck_hash_table(&groups, NOTINDEX, 256, sizeof(struct chck_iter_pool))))
      return false;

   {
      struct chck_iter_pool *pool;
      if ((pool = get_group(name)))
         return (exists_in_pool(pool, handle) ? false : chck_iter_pool_push_back(pool, &handle));
   }

   struct chck_iter_pool pool;
   if (!chck_iter_pool(&pool, 1, 0, sizeof(plugin_h)))
      return false;

   if (!chck_iter_pool_push_back(&pool, &handle) || !chck_hash_table_str_set(&groups, name, strlen(name), &pool))
      goto error0;

   return true;

error0:
   chck_iter_pool_release(&pool);
   return false;
}

static bool
deload_plugin(struct plugin *p, bool force)
{
   assert(p);

   if (!p->loaded && !p->handle)
      return true;

   struct chck_string *s;
   chck_iter_pool_for_each(&p->needed, s) {
      struct plugin *d;
      if ((d = get(s->data)) && d->loaded) {
         if (force) {
            deload_plugin(d, force);
         } else {
            wlc_log(WLC_LOG_ERROR, "Could not deload plugin, needed by '%s'", s->data);
            return false;
         }
      }
   }

   chck_iter_pool_for_each_call(&p->needed, chck_string_release);
   chck_iter_pool_release(&p->needed);

   if (p->info.name)
      unlink_name(p->info.name);

   if (p->loaded && p->deinit)
      p->deinit();

   if (p->handle)
      chck_dl_unload(p->handle);

   p->handle = NULL;
   p->loaded = false;
   return true;
}

static bool load_plugin(struct plugin *p);

static bool
load_dep(struct plugin *p, struct plugin *d, bool hard)
{
   assert(p && d);

   if (!d->loaded && !load_plugin(d) && hard)
      return false;

   struct chck_string name = {0};
   if (!chck_string_set_cstr(&name, p->info.name, true))
      return false;

   if (!chck_iter_pool_push_back(&d->needed, &name))
      goto error0;

   return true;

error0:
   chck_string_release(&name);
   return false;
}

static bool
load_deps_from_group(struct plugin *p, struct chck_iter_pool *pool, bool hard)
{
   assert(p && pool);
   plugin_h *h;
   chck_iter_pool_for_each(pool, h) {
      struct plugin *d = chck_pool_get(&plugins, *h);
      if (d && !load_dep(p, d, hard))
         return false;
   }
   return true;
}

static bool
belongs_to_group(struct plugin *p, const char *name)
{
   assert(p && name);
   for (uint32_t i = 0; p->info.groups && p->info.groups[i]; ++i) {
      if (chck_cstreq(p->info.groups[i], name))
         return true;
   }
   return false;
}

static bool
load_deps_from_array(struct plugin *p, const char **array, bool hard)
{
   assert(p);

   for (uint32_t i = 0; array && array[i]; ++i) {
      struct chck_iter_pool *pool;
      if (!belongs_to_group(p, array[i]) && (pool = get_group(array[i])))
         return load_deps_from_group(p, pool, hard);

      struct plugin *d;
      if (!(d = get(array[i])) && hard) {
         wlc_log(WLC_LOG_ERROR, "Dependency '%s' for plugin '%s' was not found", array[i], p->info.name);
         return false;
      }

      if (d && !load_dep(p, d, hard))
         return false;
   }

   return true;
}

static bool
load_plugin(struct plugin *p)
{
   assert(p);

   if (p->loaded)
      return true;

   if (!load_deps_from_array(p, p->info.requires, true) ||
       !load_deps_from_array(p, p->info.after, false))
      goto error0;

   if (p->init && !p->init())
      goto error0;

   return (p->loaded = true);

error0:
   wlc_log(WLC_LOG_ERROR, "Plugin '%s' failed to load", p->info.name);
   deload_plugin(p, false);
   return false;
}

static void
plugin_release(struct plugin *p)
{
   assert(p);
   deload_plugin(p, true);
   chck_string_release(&p->path);
}

void
deload_plugins(void)
{
   struct plugin *p;
   chck_pool_for_each(&plugins, p)
      plugin_release(p);

   chck_pool_release(&plugins);
   chck_hash_table_release(&names);
   wlc_log(WLC_LOG_INFO, "Deloaded plugins");
}

void
load_plugins(void)
{
   struct plugin *p;
   size_t loaded = 0, count = plugins.items.count;
   chck_pool_for_each(&plugins, p) {
      if (!load_plugin(p)) {
         chck_pool_remove(&plugins, _I - 1);
         continue;
      }
      ++loaded;
   }

   wlc_log(WLC_LOG_INFO, "Loaded %zu/%zu plugins", loaded, count);
}

enum conflict_msg {
   registered,
   conflict,
   group
};

static bool
exists_in_info_array(const char *name, const char **array, bool check_group, enum conflict_msg msg)
{
   assert(name);

   for (uint32_t i = 0; array && array[i]; ++i) {
      struct plugin *p;
      if ((p = get(array[i])) || (check_group && get_group(array[i]))) {
         if (msg == registered) {
            wlc_log(WLC_LOG_ERROR, "%s with name '%s' is already registered", (p ? "Plugin" : "Group"), array[i]);
         } else if (msg == conflict) {
            wlc_log(WLC_LOG_ERROR, "Plugin '%s' conflicts with %s '%s'", (p ? "plugin" : "group"), name, array[i]);
         } else if (msg == group) {
            if (get_group(array[i])) // check if the conflicted package belongs to the group as well
               return false;         // if that is true, we can ignore this conflict.
            wlc_log(WLC_LOG_ERROR, "Group '%s' conflicts with plugin '%s'", array[i], p->info.name);
         }
         return true;
      }
   }

   return false;
}

bool
register_plugin(struct plugin *plugin, const struct plugin_info* (*reg)(void))
{
   assert(plugin);

   if (reg) {
      const struct plugin_info *info;
      if (!(info = reg()))
         goto error0;

      if (!info->name || !info->description) {
         wlc_log(WLC_LOG_ERROR, "Plugin with no name or description is not allowed");
         goto error0;
      }

      struct plugin *p;
      if ((p = get(info->name))) {
         wlc_log(WLC_LOG_ERROR, "Plugin with name '%s' is already registered", info->name);
         goto error0;
      }

      if (exists_in_info_array(info->name, info->provides, true, registered) ||
          exists_in_info_array(info->name, info->conflicts, true, conflict) ||
          exists_in_info_array(info->name, info->groups, false, group))
         goto error0;

      memcpy(&plugin->info, info, sizeof(plugin->info));
   }

   if (!plugins.items.member && !chck_pool(&plugins, 1, 0, sizeof(struct plugin)))
      goto error0;

   plugin_h handle;
   if (!chck_iter_pool(&plugin->needed, 1, 0, sizeof(struct chck_string)) || !chck_pool_add(&plugins, plugin, &handle))
      goto error0;

   if (!link_name(plugin->info.name, handle))
      goto error1;

   if (plugin->info.groups) {
      for (uint32_t i = 0; plugin->info.groups[i]; ++i) {
         if (!add_to_group(plugin->info.groups[i], handle))
            goto error2;
      }
   }

   wlc_log(WLC_LOG_INFO, "registered plugin %s (%s) %s", plugin->info.name, plugin->info.version, plugin->info.description);
   return true;

error2:
   unlink_name(plugin->info.name);
   for (uint32_t i = 0; plugin->info.groups[i]; ++i)
      remove_from_group(plugin->info.groups[i], handle);
error1:
   chck_pool_remove(&plugins, handle);
error0:
   plugin_release(plugin);
   return false;
}

bool
register_plugin_from_path(const char *path)
{
   assert(path);

   void *handle;
   const char *error;
   if (!(handle = chck_dl_load(path, &error))) {
      wlc_log(WLC_LOG_ERROR, "%s", error);
      return false;
   }

   void *methods[3];
   const void *names[3] = { "plugin_register", "plugin_init", "plugin_deinit" };
   for (int32_t i = 0; i < 3; ++i) {
      if (!(methods[i] = chck_dl_load_symbol(handle, names[i], &error))) {
         wlc_log(WLC_LOG_ERROR, "%s", error);
         chck_dl_unload(&handle);
         return false;
      }
   }

   struct plugin p;
   memset(&p, 0, sizeof(p));
   p.init = methods[1];
   p.deinit = methods[2];
   p.handle = handle;
   return (chck_string_set_cstr(&p.path, path, true) && register_plugin(&p, methods[0]));
}

plugin_h
import_plugin(const char *name)
{
   if (!name)
      return 0;

   const plugin_h h = get_handle(name);
   return (h == NOTINDEX ? 0 : h + 1);
}

bool
has_methods(plugin_h handle, const struct method_info *methods)
{
   if (!handle || !methods)
      return false;

   struct plugin *p;
   if (!(p = chck_pool_get(&plugins, handle - 1)))
      return false;

   for (size_t x = 0; methods[x].name; ++x) {
      bool found = false;
      for (uint32_t i = 0; p->info.methods[i].info.name && p->info.methods[i].info.signature; ++i) {
         const struct method *m = &p->info.methods[i];
         if (!chck_cstreq(m->info.name, methods[x].name))
            continue;

         found = chck_cstreq(m->info.signature, methods[x].signature);
         break;
      }

      if (!found) {
         wlc_log(WLC_LOG_WARN, "No such method %s in %s (%s) or wrong signature", methods[x].name, p->info.name, p->info.version);
         return false;
      }
   }

   return true;
}

void*
import_method(plugin_h handle, const char *name, const char *signature)
{
   if (!handle || !name || !signature)
      return NULL;

   struct plugin *p;
   if (!(p = chck_pool_get(&plugins, handle - 1)))
      return NULL;

   for (uint32_t i = 0; p->info.methods[i].info.name && p->info.methods[i].info.signature; ++i) {
      const struct method *m = &p->info.methods[i];
      if (chck_cstreq(m->info.name, name)) {
         if (!chck_cstreq(m->info.signature, signature)) {
            wlc_log(WLC_LOG_WARN, "Method '%s' '%s' != '%s' signature mismatch in %s (%s)", name, signature, m->info.signature, p->info.name, p->info.version);
            return NULL;
         }

         if (m->deprecated)
            wlc_log(WLC_LOG_WARN, "Method '%s' is deprecated in %s (%s)", name, p->info.name, p->info.version);

         return m->function;
      }
   }

   wlc_log(WLC_LOG_WARN, "No such method '%s' in %s (%s)", name, p->info.name, p->info.version);
   return NULL;
}
