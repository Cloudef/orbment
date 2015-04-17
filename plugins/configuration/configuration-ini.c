#include <orbment/plugin.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <chck/xdg/xdg.h>
#include <chck/string/string.h>
#include <inihck/inihck.h>
#include "config.h"

static bool (*add_configuration_backend)(plugin_h loader, const char *name, const struct function *get);

static struct {
   struct ini inif;
   plugin_h self;
} plugin;

static bool
convert_key(struct chck_string *converted, const char *key)
{
   assert(converted && key);

   if (!key[1] || !chck_string_set_cstr(converted, key + 1, true))
      return false;

   if (chck_string_is_empty(converted))
      goto error0;

   char *s = strchr(converted->data, '/');
   for (s = (s ? strchr(s + 1, '/') : NULL); s && *s; s = strchr(s, '/'))
      *s = '.';

   return true;

error0:
   chck_string_release(converted);
   return false;
}

static bool
get(const char *key, const char type, void *value_out)
{
   assert(key && type && strchr("idsb", type));

   struct chck_string converted = {0};
   if (!convert_key(&converted, key))
      return false;

   struct ini_value v;
   if (!ini_get(&plugin.inif, converted.data, &v))
      goto not_found;

   chck_string_release(&converted);

   if (chck_cstr_is_empty(v.data))
      return false;

   switch(type) {
      case 's':
         *(const char**)value_out = v.data;
         break;

      case 'i': return chck_cstr_to_i32(v.data, value_out);
      case 'd': return chck_cstr_to_d(v.data, value_out);
      case 'b': return chck_cstr_to_bool(v.data, value_out);

      default:
         assert(false && "there should always be a valid type");
         return false;
   }

   return true;

not_found:
   chck_string_release(&converted);
   return false;
}

static bool
get_config_path(struct chck_string *path)
{
   assert(path);
   char *config_dir = xdg_get_path("XDG_CONFIG_HOME", ".config");
   static const char *suffix = "orbment/orbment.ini";
   const bool ret = (!chck_cstr_is_empty(config_dir) && chck_string_set_format(path, "%s/%s", config_dir, suffix));
   free(config_dir);
   return ret;
}

static void
throw(struct ini *ini, size_t line_num, size_t position, const char *line, const char *message)
{
   (void)ini;
   plog(plugin.self, PLOG_ERROR, "[%zu, %zu]: %s", line_num, position, message);
   plog(plugin.self, PLOG_ERROR, "%s", line);
   plog(plugin.self, PLOG_ERROR, "%*c", (uint32_t)position, '^');
}

void
plugin_deinit(plugin_h self)
{
   (void)self;
   ini_release(&plugin.inif);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h configuration;
   if (!(configuration = import_plugin(self, "configuration")))
      return false;

   if (!(add_configuration_backend = import_method(self, configuration, "add_configuration_backend", "b(h,c[],fun)|1")))
      return false;

   if (!add_configuration_backend(self, "INI", FUN(get, "b(c[],c,v)|1")))
      return false;

   if (!ini(&plugin.inif, '/', 256, throw))
      return false;

   struct chck_string path = {0};
   if (!get_config_path(&path))
      return false;

   if (access(path.data, R_OK)) {
      plog(self, PLOG_WARN, "Failed to open '%s': %s", path.data, strerror(errno));
      chck_string_release(&path);
      return false;
   }

   const struct ini_options options = { .escaping = true, .quoted_strings = true, .empty_values = true };
   const bool ret = ini_parse(&plugin.inif, path.data, &options);

   if (!ret)
      plog(self, PLOG_ERROR, "Failed to parse '%s'", path.data);

   chck_string_release(&path);
   return ret;
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
