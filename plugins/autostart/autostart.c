#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wlc/wlc.h>
#include <orbment/plugin.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include "config.h"
#include <wlc/wlc.h>

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
   struct chck_string key = {0}, command = {0};
   struct chck_iter_pool argv;

   if (!chck_iter_pool(&argv, 4, 4, sizeof(char*)))
      return;

   for (uint32_t i = 0; ; i++) {
      if (!chck_string_set_format(&key, "/autostart/%u", i))
         break;

      const char *command_cstr;
      if (!plugin.configuration_get(key.data, 's', &command_cstr))
         break;
      if (!chck_string_set_cstr(&command, command_cstr, true))
         break;

      char *t;
      const char *state = NULL, *null = NULL;
      size_t len;
      while ((t = (char*)chck_cstr_tokenize_quoted(command.data, &len, " ", "\"'", &state))) {
         chck_iter_pool_push_back(&argv, &t);
         /* terminate the previous token with \0 (if this isn't the first token) */
         if (t != command.data) {
            *(t-1) = '\0';
         }
      }

      plog(plugin.self, PLOG_INFO, "spawning '%s'.", command_cstr);
      chck_iter_pool_push_back(&argv, &null); /* NULL indicates end of the array */
      wlc_exec(command.data, chck_iter_pool_to_c_array(&argv, NULL));
      chck_iter_pool_empty(&argv);
   }

   chck_string_release(&key);
   chck_string_release(&command);
   chck_iter_pool_release(&argv);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h configuration;
   if ((configuration = import_plugin(self, "configuration"))) {
      plugin.configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1");
   } else {
      plog(plugin.self, PLOG_ERROR, "Cannot load autostart commands: plugin 'configuration' not loaded.");
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
