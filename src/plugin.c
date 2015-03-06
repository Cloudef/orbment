#include "plugin.h"
#include <wlc.h>
#include <stdio.h>
#include <assert.h>
#include "chck/dl/dl.h"
#include "chck/pool/pool.h"

static struct chck_pool plugins;

static bool
exists(const char *name, struct plugin **out_p)
{
   assert(name);
   if (out_p) *out_p = NULL;

   struct plugin *p;
   chck_pool_for_each(&plugins, p) {
      if (p->handle && chck_cstreq(p->info.name, name)) {
         if (out_p) *out_p = p;
         return true;
      }
   }

   return false;
}

static bool
deload_plugin(struct plugin *p, bool force)
{
   if (!p->loaded)
      return true;

   struct chck_string *s;
   chck_iter_pool_for_each(&p->needed, s) {
      struct plugin *d;
      if (exists(s->data, &d) && d->loaded) {
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

   if (p->deinit)
      p->deinit();

   if (p->handle)
      chck_dl_unload(p->handle);

   p->handle = NULL;
   p->loaded = false;
   return true;
}

static bool
load_plugin(struct plugin *p)
{
   if (p->loaded)
      return true;

   if (p->info.requires) {
      for (uint32_t i = 0; p->info.requires[i]; ++i) {
         struct plugin *d;
         if (!exists(p->info.requires[i], &d)) {
            wlc_log(WLC_LOG_ERROR, "Dependency '%s' for plugin '%s' was not found", p->info.requires[i], p->info.name);
            return false;
         }

         if (!d->loaded && !load_plugin(d))
            return false;

         struct chck_string name = {0};
         chck_string_set_cstr(&name, p->info.name, true);
         chck_iter_pool_push_back(&d->needed, &name);
      }
   }

   if (p->info.after) {
      for (uint32_t i = 0; p->info.after[i]; ++i) {
         struct plugin *d;
         if (exists(p->info.after[i], &d)) {
            if (load_plugin(d)) {
               struct chck_string name = {0};
               chck_string_set_cstr(&name, p->info.name, true);
               chck_iter_pool_push_back(&d->needed, &name);
            }
         }
      }
   }

   if (p->init && !p->init()) {
      wlc_log(WLC_LOG_ERROR, "Plugin '%s' failed to load", p->info.name);
      deload_plugin(p, false);
      return false;
   }

   return (p->loaded = true);
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
   wlc_log(WLC_LOG_INFO, "Deloaded plugins");
}

void
load_plugins(void)
{
   size_t loaded = 0, count = plugins.items.count;
   struct plugin *p;
   chck_pool_for_each(&plugins, p) {
      if (!load_plugin(p)) {
         chck_pool_remove(&plugins, _I - 1);
         continue;
      }
      ++loaded;
   }

   wlc_log(WLC_LOG_INFO, "Loaded %zu/%zu plugins", loaded, count);
}

bool
register_plugin(struct plugin *plugin, const struct plugin_info* (*reg)(void))
{
   assert(plugin);

   if (reg) {
      const struct plugin_info *info;
      if (!(info = reg()))
         return false;

      if (exists(info->name, NULL)) {
         wlc_log(WLC_LOG_ERROR, "Plugin with name '%s' is already registered", info->name);
         return false;
      }

      if (info->provides) {
         for (uint32_t i = 0; info->provides[i]; ++i) {
            if (exists(info->provides[i], NULL)) {
               wlc_log(WLC_LOG_ERROR, "Plugin with name '%s' is already registered", info->provides[i]);
               return false;
            }
         }
      }

      if (info->conflicts) {
         for (uint32_t i = 0; info->conflicts[i]; ++i) {
            if (exists(info->conflicts[i], NULL)) {
               wlc_log(WLC_LOG_ERROR, "Plugin '%s' conflicts with plugin '%s'", info->name, info->conflicts[i]);
               return false;
            }
         }
      }

      memcpy(&plugin->info, info, sizeof(plugin->info));
   }

   if (!plugins.items.member && !chck_pool(&plugins, 1, 0, sizeof(struct plugin)))
      return false;

   if (!chck_iter_pool(&plugin->needed, 1, 0, sizeof(struct chck_string)))
      return false;

   if (!chck_pool_add(&plugins, plugin, NULL))
      return false;

   wlc_log(WLC_LOG_INFO, "registered plugin %s (%s) %s", plugin->info.name, plugin->info.version, plugin->info.description);
   return true;
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
   chck_string_set_cstr(&p.path, path, true);

   if (!register_plugin(&p, methods[0])) {
      plugin_release(&p);
      return false;
   }

   return true;
}

plugin_h
import_plugin(const char *name)
{
   if (!name)
      return 0;

   struct plugin *p;
   chck_pool_for_each(&plugins, p) {
      if (chck_cstreq(p->info.name, name))
         return _I;
   }

   return 0;
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
