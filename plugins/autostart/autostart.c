#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wlc/wlc.h>
#include <orbment/plugin.h>
#include <chck/string/string.h>
#include "config.h"

typedef bool (*configuration_get_fun_t)(const char *key, const char type, void *value_out);

struct autostart_backend {
   plugin_h handle;
   const char *name;
   configuration_get_fun_t get;
};

static struct {
   plugin_h self;
   configuration_get_fun_t configuration_get;
   bool (*add_hook)(plugin_h, const char *name, const struct function*);
} plugin;

void
do_autostart(void)
{
   struct chck_string key = {0};

   for (uint32_t i = 0; ; i++) {
      if (!chck_string_set_format(&key, "/autostart/%u", i))
         break;

      char *command;
      if (!plugin.configuration_get(key.data, 's', &command))
         break;

      wlc_exec(command, (char *const[]){ command, NULL });
   }

   chck_string_release(&key);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h configuration;
   if ((configuration = import_plugin(self, "configuration"))) {
      plugin.configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1");
   } else {
      plog(0, PLOG_ERROR, "Cannot load autostart commands: plugin 'configuration' not loaded.");
      return false;
   }

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(plugin.add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   return (plugin.add_hook(self, "compositor.ready", FUN(do_autostart, "v()|1")));
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "orbment",
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "autostart",
      .description = "Autostart programs when orbment starts.",
      .version = VERSION,
      .requires = requires
   };

   return &info;
}
