#include "plugin.h"
#include <wlc.h>
#include <stdio.h>

#include "chck/dl/dl.h"
#include "chck/pool/pool.h"

static struct chck_pool plugins;

bool
register_plugin(struct plugin *plugin, const struct plugin_info* (*reg)(void))
{
   if (reg) {
      const struct plugin_info *info;
      if (!(info = reg()))
         return false;

      memcpy(&plugin->info, info, sizeof(plugin->info));
   }

   if (!plugins.items.member && !chck_pool(&plugins, 1, 0, sizeof(struct plugin)))
      return false;

   if (!chck_pool_add(&plugins, plugin, NULL))
      return false;

   wlc_log(WLC_LOG_INFO, "registered plugin %s (%s)", plugin->info.name, plugin->info.version);
   return true;
}

bool
register_plugin_from_path(const char *path)
{
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
   if (!register_plugin(&p, methods[0])) {
      chck_dl_unload(&handle);
      return false;
   }

   p.init = methods[1];
   p.deinit = methods[2];
   p.handle = handle;
   chck_string_set_cstr(&p.path, path, true);
   p.init();
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
         wlc_log(WLC_LOG_WARN, "no such method %s in %s (%s)", methods[x].name, p->info.name, p->info.version);
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
