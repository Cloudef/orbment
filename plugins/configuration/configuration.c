#include <orbment/plugin.h>
#include <chck/string/string.h>
#include "config.h"

static const char get_sig[] = "b(c[],c,v)|1";
typedef bool (*get_fun_t)(const char *key, const char type, void *value_out);

struct configuration_backend {
   plugin_h handle;
   const char *name;
   get_fun_t get;
};

static struct {
   plugin_h self;
   struct configuration_backend backend;
} plugin;

static bool
add_configuration_backend(plugin_h caller, const char *name, const struct function *get)
{
   if (plugin.backend.name) {
      plog(plugin.self, PLOG_WARN, "Configuration backend '%s' already loaded.", plugin.backend.name);
      return false;
   }

   if (!get)
      return false;

   if (!chck_cstreq(get->signature, get_sig)) {
      plog(plugin.self, PLOG_ERROR, "Wrong signature provided for configuration backend get() (%s != %s)", get->signature, get_sig);
      return false;
   }

   plugin.backend.get = get->function;
   plugin.backend.name = name;

   return true;
}

static bool
get(const char *key, char type, void *value_out)
{
   if (!plugin.backend.name) {
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': configuration backend not loaded.", key);
      return false;
   }

   if (chck_cstr_is_empty(key)) {
      plog(plugin.self, PLOG_WARN, "Cannot get NULL/empty key.");
      return false;
   }

   /* TODO: validate key further */ 

   if (!type || !strchr("i", type)) {
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': invalid type character '%c'.", key, type);
      return false;
   }

   return plugin.backend.get(key, type, value_out);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;
   plugin.backend.name = NULL;

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_configuration_backend, "b(h,c[],fun)|1"),
      REGISTER_METHOD(get, "b(c[],c,v)|1"),
      {0}
   };

   static const char *groups[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "configuration",
      .description = "Configuration API.",
      .version = VERSION,
      .methods = methods,
      .groups = groups
   };

   return &info;
}
