#include <orbment/plugin.h>
#include <chck/string/string.h>
#include "config.h"

static const char get_sig[] = "b(v,c[],c)|1";
typedef bool (*get_fun_t)(void *value_out, const char *key, const char type);

struct configuration_backend {
   get_fun_t get;
};

static struct {
   plugin_h self;
   struct configuration_backend backend;
   bool backend_loaded;
} plugin;

static bool
add_configuration_backend(const struct function *get)
{
   if (plugin.backend_loaded) {
      plog(plugin.self, PLOG_WARN, "Configuration backend already loaded.");
      return false;
   }

   if (!get)
      return false;

   if (!chck_cstreq(get->signature, get_sig)) {
      plog(plugin.self, PLOG_ERROR, "Wrong signature provided for configuration backend get() (%s != %s)", get->signature, get_sig);
      return false;
   }

   plugin.backend.get = get->function;
   plugin.backend_loaded = true;

   return true;
}

static bool
get(void* value_out, const char *key, char type)
{
   if (!plugin.backend_loaded) {
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': configuration backend not loaded.", key);
      return false;
   }

   if (!key || *key == '\0') {
      plog(plugin.self, PLOG_WARN, "Cannot get NULL/empty key.");
      return false;
   }

   /* TODO: validate key further */ 

   if (!type || !strchr("i", type)) {
      plog(plugin.self, PLOG_WARN, "Cannot get key '%s': invalid type character '%c'.", key, type);
      return false;
   }

   return plugin.backend.get(value_out, key, type);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;
   plugin.backend_loaded = false;

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_configuration_backend, "b(fun)|1"),
      REGISTER_METHOD(get, "b(v,c[],c)|1"),
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
