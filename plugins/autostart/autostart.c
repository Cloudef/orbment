#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wlc/wlc.h>
#include <orbment/plugin.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include "config.h"
#include <wlc/wlc.h>

static bool (*configuration_get)(const char *key, const char type, void *value_out);
static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   plugin_h self;
} plugin;

static void
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
      if (!configuration_get(key.data, 's', &command_cstr))
         break;

      if (!chck_string_set_cstr(&command, command_cstr, true))
         break;

      char *t;
      size_t len;
      const char *state = NULL;
      while ((t = (char*)chck_cstr_tokenize_quoted(command.data, &len, " ", "\"'", &state))) {
         chck_iter_pool_push_back(&argv, &t);
         t[len] = 0; /* replaces each token with \0 */
      }

      const char *null = NULL;
      chck_iter_pool_push_back(&argv, &null); /* NULL indicates end of the array */
      plog(plugin.self, PLOG_INFO, "spawning: %s", command_cstr);
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

   plugin_h orbment, configuration;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(configuration = import_plugin(self, "configuration")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")) ||
       !(configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1")))
      return false;

   return add_hook(self, "compositor.ready", FUN(do_autostart, "v(v)|1"));
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "autostart",
      .description = "Launch programs on startup.",
      .version = VERSION,
      .requires = requires
   };

   return &info;
}
