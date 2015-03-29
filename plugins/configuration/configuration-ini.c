#include <orbment/plugin.h>
#include <chck/xdg/xdg.h>
#include <chck/string/string.h>
#include "ciniparser/ciniparser.h"
#include "config.h"

static bool (*add_configuration_backend)(plugin_h loader, const char *name, const struct function *get);

static dictionary *dict; 

static bool
ini_get(const char *key, const char type, void *value_out)
{
   if (!dict)
      return false;

   if (!ciniparser_find_entry(dict, key))
      return false;

   if (!value_out)
      return true;

   switch(type) {
      case 'i':
         *(int*)value_out = ciniparser_getint(dict, key, 0);
         break;
      case 'd':
         *(double*)value_out = ciniparser_getdouble(dict, key, 0.0);
         break;
      case 's':
         *(char**)value_out = ciniparser_getstring(dict, key, "");
         break;
   }

   return true;
}

static bool
get_config_path(struct chck_string *path)
{
   const char *config_dir = xdg_get_path("XDG_CONFIG_HOME", ".config");
   static const char *suffix = "orbment/orbment.ini";

   if (chck_cstr_is_empty(config_dir))
      return false;

   if (!chck_string_set_format(path, "%s/%s", config_dir, suffix))
      return false;

   return true;
}

bool
plugin_init(plugin_h self)
{
   plugin_h configuration;
   struct chck_string path = {0};

   if (!(configuration = import_plugin(self, "configuration")))
      return false;

   if (!(add_configuration_backend = import_method(self, configuration, "add_configuration_backend", "b(h,c[],fun)|1")))
      return false;

   if (!add_configuration_backend(self, "INI", FUN(ini_get, "b(c[],c,v)|1")))
      return false;

   if (!get_config_path(&path))
      return false;

   if (!(dict = ciniparser_load(path.data)))
      plog(self, PLOG_WARN, "Cannot open '%s'.", path.data);

   chck_string_release(&path);

   return true;
}

void
plugin_deinit(plugin_h self)
{
   (void)self;
   ciniparser_freedict(dict);
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "configuration",
      NULL,
   };

   static const char *provides[] = {
      "configuration-backend",
      NULL,
   };

   static const char *groups[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "configuration-ini",
      .description = "Configuration backend for the INI format.",
      .version = VERSION,
      .requires = requires,
      .provides = provides,
      .groups = groups,
   };

   return &info;
}
