#include <orbment/plugin.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <chck/xdg/xdg.h>
#include <chck/string/string.h>
#include <chck/overflow/overflow.h>
#include <inihck/inihck.h>
#include "config.h"

static bool (*add_configuration_backend)(plugin_h loader, const char *name, const struct function *get, const struct function *list);

static const char *pair_sig = "c[],c[]|1";

struct pair {
   char *key, *value;
};

static struct {
   plugin_h self;
} plugin;

static void
throw(struct ini *ini, size_t line_num, size_t position, const char *line, const char *message)
{
   (void)ini;
   plog(plugin.self, PLOG_ERROR, "[%zu, %zu]: %s", line_num, position, message);
   plog(plugin.self, PLOG_ERROR, "%s", line);
   plog(plugin.self, PLOG_ERROR, "%*c", (uint32_t)position, '^');
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

static bool
convert_key(struct chck_string *converted, const char *key)
{
   assert(converted && key);

   if (chck_cstr_is_empty(key))
      return false;

   if (!key[1] || !chck_string_set_format(converted, "/%s", key))
      return false;

   char *s = converted->data + 1;
   for (s = strchr(converted->data, '.'); s && *s; s = strchr(s, '.'))
      *s = '/';

   return true;
}

bool
save(const char *stsign, struct pair *pairs, size_t memb)
{
   (void)pairs, (void)memb;

   if (!chck_cstreq(stsign, pair_sig)) {
      plog(plugin.self, PLOG_WARN, "Wrong struct signature. (%s != %s)", pair_sig, stsign);
      return NULL;
   }

   plog(plugin.self, PLOG_WARN, "configuration-ini does not support saving");
   return false;
}

struct pair*
load(const char *stsign, size_t *out_memb)
{
   if (out_memb)
      *out_memb = 0;

   if (!chck_cstreq(stsign, pair_sig)) {
      plog(plugin.self, PLOG_WARN, "Wrong struct signature. (%s != %s)", pair_sig, stsign);
      return NULL;
   }

   struct chck_string path = {0};
   if (!get_config_path(&path))
      return false;

   if (access(path.data, R_OK)) {
      plog(plugin.self, PLOG_WARN, "Failed to open '%s': %s", path.data, strerror(errno));
      goto error0;
   }

   struct ini inif;
   if (!ini(&inif, '/', 256, throw))
      goto error0;

   const struct ini_options options = { .escaping = true, .quoted_strings = true, .empty_values = true };
   if (!ini_parse(&inif, path.data, &options)) {
      plog(plugin.self, PLOG_ERROR, "Failed to parse '%s'", path.data);
      goto error1;
   }

   chck_string_release(&path);

   size_t keys = 0;
   struct ini_value v;
   ini_for_each(&inif, &v)
      ++keys;

   struct pair *pairs;
   if (!(pairs = chck_calloc_of(keys, sizeof(struct pair))))
      goto error1;

   size_t i = 0;
   ini_for_each(&inif, &v) {
      struct chck_string converted = {0}, value = {0};
      if (!convert_key(&converted, _I.path)) {
         chck_string_release(&converted);
         continue;
      }

      chck_string_set_cstr_with_length(&value, v.data, v.size, true);
      pairs[i++] = (struct pair){ converted.data, value.data };
   }

   if (out_memb)
      *out_memb = keys;

   // Memory for pairs and the contained strings is now owned by configuration plugin
   ini_release(&inif);
   return pairs;

error1:
   ini_release(&inif);
error0:
   chck_string_release(&path);
   return NULL;
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h configuration;
   if (!(configuration = import_plugin(self, "configuration")))
      return false;

   if (!(add_configuration_backend = import_method(self, configuration, "add_configuration_backend", "b(h,c[],fun,fun)|1")))
      return false;

   if (!add_configuration_backend(self, "INI", FUN(load, "*(c[],sz*)|1"), FUN(save, "b(c[],*,sz)|1")))
      return false;

   return true;
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
