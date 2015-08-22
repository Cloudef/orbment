#include <orbment/plugin.h>
#include <xkbcommon/xkbcommon.h>
#include <chck/math/math.h>
#include <chck/pool/pool.h>
#include <chck/lut/lut.h>
#include <chck/string/string.h>
#include "common.h"
#include "config.h"

#include <linux/input.h>

static const size_t NOTINDEX = (size_t)-1;

static bool (*add_hook)(plugin_h, const char *name, const struct function*);
static bool (*configuration_get)(const char *key, const char type, void *value_out);

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
struct keybind {
   struct chck_string name;
   const char **defaults;
   keybind_fun_t function;
   intptr_t arg;
   plugin_h owner;
};

static struct {
   struct {
      struct chck_pool pool;
      struct chck_hash_table table;
   } keybinds;

   uint32_t prefix;
   plugin_h self;
} plugin;

static uint32_t
parse_prefix(const char *str)
{
   // default prefix
   const uint32_t def = (wlc_get_backend_type() == WLC_BACKEND_X11 ? WLC_BIT_MOD_ALT : WLC_BIT_MOD_LOGO);

   if (!str)
      return def;

   static const struct {
      const char *name;
      enum wlc_modifier_bit mod;
   } map[] = {
      { "shift", WLC_BIT_MOD_SHIFT },
      { "caps", WLC_BIT_MOD_CAPS },
      { "ctrl", WLC_BIT_MOD_CTRL },
      { "alt", WLC_BIT_MOD_ALT },
      { "mod2", WLC_BIT_MOD_MOD2 },
      { "mod3", WLC_BIT_MOD_MOD3 },
      { "logo", WLC_BIT_MOD_LOGO },
      { "mod5", WLC_BIT_MOD_MOD5 },
      { NULL, 0 },
   };

   uint32_t prefix = 0;
   const char *s = str;
   for (int i = 0; map[i].name && *s; ++i) {
      if (!chck_cstreq(map[i].name, s))
         continue;

      prefix |= map[i].mod;
   }

   return (prefix ? prefix : def);
}

static bool
syntax_append(struct chck_string *syntax, const char *cstr, bool is_heap)
{
   assert(syntax && cstr);

   if (syntax->size > 0)
      return chck_string_set_format(syntax, "%s-%s", syntax->data, cstr);

   return chck_string_set_cstr(syntax, cstr, is_heap);
}

static bool
append_mods(struct chck_string *syntax, struct chck_string *prefixed, uint32_t mods)
{
   assert(syntax && prefixed);

   if (mods == plugin.prefix && !syntax_append(prefixed, "P", false))
      return false;

   static const struct {
      const char *name;
      enum wlc_modifier_bit mod;
   } map[] = {
      { "S", WLC_BIT_MOD_SHIFT },
      { "C", WLC_BIT_MOD_CTRL },
      { "M", WLC_BIT_MOD_ALT },
      { "L", WLC_BIT_MOD_LOGO },
      { "M2", WLC_BIT_MOD_MOD2 },
      { "M3", WLC_BIT_MOD_MOD3 },
      { "M5", WLC_BIT_MOD_MOD5 },
      { NULL, 0 },
   };

   for (uint32_t i = 0; map[i].name; ++i) {
      if (!(mods & map[i].mod))
         continue;

      if (!syntax_append(syntax, map[i].name, false))
         return false;
   }

   return true;
}

static bool
keybind_exists(const char *name)
{
   const struct keybind *k;
   chck_pool_for_each(&plugin.keybinds.pool, k) {
      if (chck_string_eq_cstr(&k->name, name))
         return true;
   }

   return false;
}

static const struct keybind*
keybind_for_syntax(const char *syntax)
{
   size_t *index;
   if (chck_cstr_is_empty(syntax) || !(index = chck_hash_table_str_get(&plugin.keybinds.table, syntax, strlen(syntax))) || *index == NOTINDEX)
      return NULL;

   return chck_pool_get(&plugin.keybinds.pool, *index);
}

static bool
add_keybind_mapping(struct chck_string *mappings, const char *syntax, size_t *index)
{
   assert(mappings && index);

   if (chck_cstr_is_empty(syntax))
      return false;

   const struct keybind *o;
   if ((o = keybind_for_syntax(syntax))) {
      plog(plugin.self, PLOG_WARN, "'%s' is already mapped to keybind '%s'", syntax, o->name.data);
      return false;
   }

   chck_hash_table_str_set(&plugin.keybinds.table, syntax, strlen(syntax), index);
   chck_string_set_format(mappings, (mappings->size > 0 ? "%s, %s" : "%s%s"), (mappings->data ? mappings->data : ""), syntax);
   return true;
}

static bool
add_keybind(plugin_h caller, const char *name, const char **syntax, const struct function *fun, intptr_t arg)
{
   if (!name || !fun || !caller)
      return false;

   static const char *signature = "v(h,u32,ip)|1";

   if (!chck_cstreq(fun->signature, signature)) {
      plog(plugin.self, PLOG_WARN, "Wrong signature provided for '%s keybind' function. (%s != %s)", name, signature, fun->signature);
      return false;
   }

   if (keybind_exists(name)) {
      plog(plugin.self, PLOG_WARN, "Keybind with name '%s' already exists", name);
      return false;
   }

   if (!plugin.keybinds.pool.items.member && !chck_pool(&plugin.keybinds.pool, 32, 0, sizeof(struct keybind)))
      return false;

   if (!plugin.keybinds.table.lut.table && !chck_hash_table(&plugin.keybinds.table, NOTINDEX, 256, sizeof(size_t)))
      return false;

   struct keybind k = {
      .defaults = syntax,
      .function = fun->function,
      .arg = arg,
      .owner = caller,
   };

   if (!chck_string_set_cstr(&k.name, name, true))
      return false;

   size_t index;
   if (!chck_pool_add(&plugin.keybinds.pool, &k, &index))
      goto error0;

   struct chck_string mappings = {0};
   bool mapped = false;

   if (configuration_get) {
      struct chck_string key = {0};
      int name_start, name_end;
      chck_string_set_format(&key, "/keybindings/%n%s%n/mappings", &name_start, name, &name_end);

      /* Configuration keys may not contain spaces, so replace spaces with underscores */
      for (int i = name_start; i < name_end; i++) {
         if (key.data[i] == ' ')
            key.data[i] = '_';
      }

      const char *value;
      if (configuration_get(key.data, 's', &value)) {
         add_keybind_mapping(&mappings, value, &index);
         mapped = true;
      }

      chck_string_release(&key);
   }

   /* If no mapping was set from configuration, try to use default keybindings */
   if (!mapped) {
      for (uint32_t i = 0; syntax && syntax[i]; ++i)
         add_keybind_mapping(&mappings, syntax[i], &index);
   }

   plog(plugin.self, PLOG_INFO, "Added keybind: %s (%s)", name, (chck_string_is_empty(&mappings) ? "none" : mappings.data));
   chck_string_release(&mappings);
   return true;

error0:
   chck_string_release(&k.name);
   return false;
}

static void
keybind_release(struct keybind *keybind)
{
   if (!keybind)
      return;

   chck_string_release(&keybind->name);
}

static void
remove_keybind(plugin_h caller, const char *name)
{
   struct keybind *k;
   chck_pool_for_each(&plugin.keybinds.pool, k) {
      if (k->owner != caller || !chck_string_eq_cstr(&k->name, name))
         continue;

      plog(plugin.self, PLOG_INFO, "Removed keybind: %s", k->name.data);
      keybind_release(k);
      chck_pool_remove(&plugin.keybinds.pool, _I - 1);
      break;
   }
}

static void
remove_keybinds_for_plugin(plugin_h caller)
{
   struct keybind *k;
   chck_pool_for_each(&plugin.keybinds.pool, k) {
      if (k->owner != caller)
         continue;

      plog(plugin.self, PLOG_INFO, "Removed keybind: %s", k->name.data);
      keybind_release(k);
      chck_pool_remove(&plugin.keybinds.pool, _I - 1);
   }
}

static void
remove_keybinds(void)
{
   chck_pool_for_each_call(&plugin.keybinds.pool, keybind_release);
   chck_pool_release(&plugin.keybinds.pool);
   chck_hash_table_release(&plugin.keybinds.table);
}

static void
plugin_deloaded(plugin_h ph)
{
   remove_keybinds_for_plugin(ph);
}

static bool
pass_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, const char name[64], bool pressed)
{
   assert(modifiers);

   bool handled = false;

   struct chck_string syntax = {0}, prefixed = {0};
   if (!append_mods(&syntax, &prefixed, modifiers->mods))
      goto out;

   syntax_append(&syntax, name, true);
   chck_string_set_format(&syntax, "<%s>", syntax.data);

   if (!chck_string_is_empty(&prefixed)) {
      syntax_append(&prefixed, name, true);
      chck_string_set_format(&prefixed, "<%s>", prefixed.data);
   }

   plog(plugin.self, PLOG_INFO, "%s combo: %s %s", (pressed ? "pressed" : "released"), syntax.data, prefixed.data);

   const struct keybind *k;
   if (!(k = keybind_for_syntax(prefixed.data)) && !(k = keybind_for_syntax(syntax.data)))
      goto out;

   if (pressed)
      k->function(view, time, k->arg);

   handled = true;

out:
   chck_string_release(&syntax);
   chck_string_release(&prefixed);
   return handled;
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
{
   (void)key;

   char name[64];
   if (xkb_keysym_get_name(sym, name, sizeof(name)) == -1)
      return false;

   const bool pressed = (state == WLC_KEY_STATE_PRESSED);
   return pass_key(view, time, modifiers, name, pressed);
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state, const struct wlc_origin *origin)
{
   (void)origin;

   bool handled = false;
   struct chck_string name = {0};
   if (!chck_string_set_format(&name, "B%u", button - BTN_MOUSE))
      goto out;

   const bool pressed = (state == WLC_BUTTON_STATE_PRESSED);
   handled = pass_key(view, time, modifiers, name.data, pressed);

out:
   chck_string_release(&name);
   return (modifiers->mods ? handled : false);
}

static const char*
load_prefix(plugin_h self)
{
   plugin_h configuration;
   if (!(configuration = import_plugin(self, "configuration")) ||
       !(configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1")))
      return NULL;

   const char *prefix;
   return (configuration_get("/keybindings/prefix", 's', &prefix) ? prefix : NULL);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   remove_keybinds();
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   plugin.prefix = parse_prefix(load_prefix(self));

   return (add_hook(self, "plugin.deloaded", FUN(plugin_deloaded, "v(h)|1")) &&
           add_hook(self, "keyboard.key", FUN(keyboard_key, "b(h,u32,*,u32,u32,e)|1")) &&
           add_hook(self, "pointer.button", FUN(pointer_button, "b(h,u32,*,u32,e,*)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_keybind, "b(h,c[],c*[],fun,ip)|1"),
      REGISTER_METHOD(remove_keybind, "v(h,c[])|1"),
      {0},
   };

   static const char *after[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "keybind",
      .description = "Keybind api.",
      .version = VERSION,
      .methods = methods,
      .after = after,
   };

   return &info;
}
