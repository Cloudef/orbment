#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <dirent.h>
#include <wlc/wlc.h>
#include <xkbcommon/xkbcommon.h>
#include <chck/pool/pool.h>
#include <chck/lut/lut.h>
#include <chck/xdg/xdg.h>
#include "plugin.h"
#include "common.h"
#include "config.h"

enum hook_type {
   HOOK_PLUGIN_LOADED,
   HOOK_PLUGIN_DELOADED,
   HOOK_OUTPUT_CREATED,
   HOOK_OUTPUT_DESTROYED,
   HOOK_OUTPUT_RESOLUTION,
   HOOK_VIEW_CREATED,
   HOOK_VIEW_DESTROYED,
   HOOK_VIEW_FOCUS,
   HOOK_VIEW_MOVE_TO_OUTPUT,
   HOOK_VIEW_GEOMETRY_REQUEST,
   HOOK_VIEW_STATE_REQUEST,
   HOOK_LAST,
};

struct hook {
   void (*function)();
   plugin_h owner;
};

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

   struct chck_iter_pool hooks[HOOK_LAST];
   uint32_t prefix;
} orbment = {
   .prefix = WLC_BIT_MOD_LOGO,
};

static bool
syntax_append(struct chck_string *syntax, const char *cstr, bool is_heap)
{
   if (syntax->size > 0)
      return chck_string_set_format(syntax, "%s-%s", syntax->data, cstr);
   return chck_string_set_cstr(syntax, cstr, is_heap);
}

static bool
append_mods(struct chck_string *syntax, struct chck_string *prefixed, uint32_t mods)
{
   if (mods == orbment.prefix && !syntax_append(prefixed, "P", false))
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
   chck_pool_for_each(&orbment.keybinds.pool, k)
      if (chck_string_eq_cstr(&k->name, name))
         return true;
   return false;
}

static const struct keybind*
keybind_for_syntax(const char *syntax)
{
   size_t *index;
   if (chck_cstr_is_empty(syntax) || !(index = chck_hash_table_str_get(&orbment.keybinds.table, syntax, strlen(syntax))) || *index == NOTINDEX)
      return NULL;

   return chck_pool_get(&orbment.keybinds.pool, *index);
}

static bool
add_keybind(plugin_h caller, const char *name, const char **syntax, const struct function *fun, intptr_t arg)
{
   if (!name || !fun || !caller)
      return false;

   static const char *signature = "v(h,u32,ip)|1";

   if (!chck_cstreq(fun->signature, signature)) {
      plog(0, PLOG_WARN, "Wrong signature provided for '%s keybind' function. (%s != %s)", name, signature, fun->signature);
      return false;
   }

   if (keybind_exists(name)) {
      plog(0, PLOG_WARN, "Keybind with name '%s' already exists", name);
      return false;
   }

   if (!orbment.keybinds.pool.items.member && !chck_pool(&orbment.keybinds.pool, 32, 0, sizeof(struct keybind)))
      return false;

   if (!orbment.keybinds.table.lut.table && !chck_hash_table(&orbment.keybinds.table, NOTINDEX, 256, sizeof(size_t)))
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
   if (!chck_pool_add(&orbment.keybinds.pool, &k, &index))
      goto error0;

   struct chck_string mappings = {0};
   for (uint32_t i = 0; syntax && syntax[i]; ++i) {
      if (chck_cstr_is_empty(syntax[i]))
         continue;

      const struct keybind *o;
      if (!(o = keybind_for_syntax(syntax[i]))) {
         chck_hash_table_str_set(&orbment.keybinds.table, syntax[i], strlen(syntax[i]), &index);
         chck_string_set_format(&mappings, (mappings.size > 0 ? "%s, %s" : "%s%s"), mappings.data, syntax[i]);
      } else {
         plog(0, PLOG_WARN, "'%s' is already mapped to keybind '%s'", syntax[i], o->name.data);
      }
   }

   plog(0, PLOG_INFO, "Added keybind: %s (%s)", name, (chck_string_is_empty(&mappings) ? "none" : mappings.data));
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
   chck_pool_for_each(&orbment.keybinds.pool, k) {
      if (k->owner != caller || !chck_string_eq_cstr(&k->name, name))
         continue;

      plog(0, PLOG_INFO, "Removed keybind: %s", k->name.data);
      keybind_release(k);
      chck_pool_remove(&orbment.keybinds.pool, _I - 1);
      break;
   }
}

static void
remove_keybinds_for_plugin(plugin_h caller)
{
   struct keybind *k;
   chck_pool_for_each(&orbment.keybinds.pool, k) {
      if (k->owner != caller)
         continue;

      plog(0, PLOG_INFO, "Removed keybind: %s", k->name.data);
      keybind_release(k);
      chck_pool_remove(&orbment.keybinds.pool, _I - 1);
   }
}

static void
remove_keybinds(void)
{
   chck_pool_for_each_call(&orbment.keybinds.pool, keybind_release);
   chck_pool_release(&orbment.keybinds.pool);
   chck_hash_table_release(&orbment.keybinds.table);
}

static bool
view_created(wlc_handle view)
{
   plog(0, PLOG_INFO, "new view: %zu (%zu)", view, wlc_view_get_parent(view));

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_CREATED], hook)
      hook->function(view);

   return true;
}

static void
view_destroyed(wlc_handle view)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_DESTROYED], hook)
      hook->function(view);

   plog(0, PLOG_INFO, "view destroyed: %zu", view);
}

static void
view_focus(wlc_handle view, bool focus)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_FOCUS], hook)
      hook->function(view, focus);
}

static void
view_move_to_output(wlc_handle view, wlc_handle from, wlc_handle to)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_MOVE_TO_OUTPUT], hook)
      hook->function(view, from, to);
}

static void
view_geometry_request(wlc_handle view, const struct wlc_geometry *geometry)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_GEOMETRY_REQUEST], hook)
      hook->function(view, geometry);
}

static void
view_state_request(wlc_handle view, const enum wlc_view_state_bit state, const bool toggle)
{
   plog(0, PLOG_INFO, "STATE: %d (%d)", state, toggle);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_STATE_REQUEST], hook)
      hook->function(view, state, toggle);
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state)
{
   (void)time, (void)modifiers, (void)button;

   // XXX: move to core-functionality
#if 0
   if (state == WLC_BUTTON_STATE_PRESSED)
      focus_view(view);
#endif

   return true;
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
{
   (void)time, (void)key;

   bool pass = true;

   struct chck_string syntax = {0}, prefixed = {0};
   if (!append_mods(&syntax, &prefixed, modifiers->mods))
      goto out;

   char name[64];
   if (xkb_keysym_get_name(sym, name, sizeof(name)) == -1)
      goto out;

   syntax_append(&syntax, name, true);
   syntax_append(&prefixed, name, true);
   chck_string_set_format(&syntax, "<%s>", syntax.data);
   chck_string_set_format(&prefixed, "<%s>", prefixed.data);
   plog(0, PLOG_INFO, "hit combo: %s %s", syntax.data, prefixed.data);

   const struct keybind *k;
   if (!(k = keybind_for_syntax(prefixed.data)) &&
       !(k = keybind_for_syntax(syntax.data)))
       goto out;

   if (state == WLC_KEY_STATE_PRESSED)
      k->function(view, time, k->arg);
   pass = false;

out:
   chck_string_release(&syntax);
   chck_string_release(&prefixed);
   return pass;
}

static bool
output_created(wlc_handle output)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_CREATED], hook)
      hook->function(output);

   return true;
}

static void
output_destroyed(wlc_handle output)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_DESTROYED], hook)
      hook->function(output);
}

static void
output_resolution(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_RESOLUTION], hook)
      hook->function(output, from, to);
}

static enum hook_type
hook_type_for_string(const char *type)
{
   struct {
      const char *name;
      enum hook_type type;
   } map[] = {
      { "plugin.loaded", HOOK_PLUGIN_LOADED },
      { "plugin.deloaded", HOOK_PLUGIN_DELOADED },
      { "output.created", HOOK_OUTPUT_CREATED },
      { "output.destroyed", HOOK_OUTPUT_DESTROYED },
      { "output.resolution", HOOK_OUTPUT_RESOLUTION },
      { "view.created", HOOK_VIEW_CREATED },
      { "view.destroyed", HOOK_VIEW_DESTROYED },
      { "view.focus", HOOK_VIEW_FOCUS },
      { "view.move_to_output", HOOK_VIEW_MOVE_TO_OUTPUT },
      { "view.geometry_request", HOOK_VIEW_GEOMETRY_REQUEST },
      { "view.state_request", HOOK_VIEW_STATE_REQUEST },
      { NULL, HOOK_LAST },
   };

   for (uint32_t i = 0; map[i].name; ++i) {
      if (chck_cstreq(type, map[i].name))
         return map[i].type;
   }

   return HOOK_LAST;
}

static void
remove_hooks_for_plugin(plugin_h caller)
{
   for (uint32_t i = 0; i < HOOK_LAST; ++i) {
      struct hook *h;
      chck_iter_pool_for_each(&orbment.hooks[i], h) {
         if (h->owner != caller)
            continue;

         chck_iter_pool_remove(&orbment.hooks[i], _I - 1);
         break;
      }
   }
}

static void
remove_hooks(void)
{
   for (uint32_t i = 0; i < HOOK_LAST; ++i)
      chck_iter_pool_release(&orbment.hooks[i]);
}

static bool
hook_exists_for_plugin(plugin_h caller, enum hook_type t)
{
   struct hook *h;
   chck_iter_pool_for_each(&orbment.hooks[t], h) {
      if (h->owner == caller)
         return true;
   }
   return false;
}

static void
remove_hook(plugin_h caller, const char *type)
{
   if (!type || !caller)
      return;

   enum hook_type t;
   if ((t = hook_type_for_string(type)) == HOOK_LAST) {
      plog(0, PLOG_WARN, "Invalid type '%s' provided for hook.", type);
      return;
   }

   struct hook *h;
   chck_iter_pool_for_each(&orbment.hooks[t], h) {
      if (h->owner != caller)
         continue;

      chck_iter_pool_remove(&orbment.hooks[t], _I - 1);
      break;
   }
}

static bool
add_hook(plugin_h caller, const char *type, const struct function *hook)
{
   if (!hook || !caller)
      return false;

   enum hook_type t;
   if ((t = hook_type_for_string(type)) == HOOK_LAST) {
      plog(0, PLOG_WARN, "Invalid type '%s' provided for hook.", type);
      return false;
   }

   if (hook_exists_for_plugin(caller, t)) {
      plog(0, PLOG_WARN, "Hook of type '%s' already exists for plugin.", type);
      return false;
   }

   static const char *signatures[HOOK_LAST] = {
      "v(h)|1", // HOOK_PLUGIN_LOADED
      "v(h)|1", // HOOK_PLUGIN_DELOADED
      "v(h)|1", // HOOK_OUTPUT_CREATED
      "v(h)|1", // HOOK_OUTPUT_DESTROYED
      "v(h,*,*)|1", // HOOK_OUTPUT_RESOLUTION
      "v(h)|1", // HOOK_VIEW_CREATED
      "v(h)|1", // HOOK_VIEW_DESTROYED
      "v(h,b)|1", // HOOK_VIEW_FOCUS
      "v(h,h,h)|1", // HOOK_VIEW_MOVE_TO_OUTPUT
      "v(h,*)|1", // HOOK_VIEW_GEOMETRY_REQUEST
      "v(h,e,b)|1", // HOOK_VIEW_STATE_REQUEST
   };

   if (!chck_cstreq(hook->signature, signatures[t])) {
      plog(0, PLOG_WARN, "Wrong signature provided for hook '%s'. (%s != %s)", type, signatures[t], hook->signature);
      return false;
   }

   if (!orbment.hooks[t].items.member && !chck_iter_pool(&orbment.hooks[t], 4, 0, sizeof(struct hook)))
      return false;

   struct hook h = {
      .function = hook->function,
      .owner = caller,
   };

   return chck_iter_pool_push_back(&orbment.hooks[t], &h);
}

static void
plugin_loaded(const struct plugin *plugin)
{
   assert(plugin);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_PLUGIN_LOADED], hook)
      hook->function(plugin->handle + 1);
}

static void
plugin_deloaded(const struct plugin *plugin)
{
   assert(plugin);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_PLUGIN_DELOADED], hook)
      hook->function(plugin->handle + 1);

   remove_keybinds_for_plugin(plugin->handle + 1);
   remove_hooks_for_plugin(plugin->handle + 1);
}

static bool
plugins_init(void)
{
   set_plugin_callbacks(plugin_loaded, plugin_deloaded);

   {
      static const struct method methods[] = {
         REGISTER_METHOD(add_keybind, "b(h,c[],c*[],fun,ip)|1"),
         REGISTER_METHOD(remove_keybind, "v(h,c[])|1"),
         REGISTER_METHOD(add_hook, "b(h,c[],fun)|1"),
         REGISTER_METHOD(remove_hook, "v(h,c[])|1"),
         {0},
      };

      struct plugin core = {
         .info = {
            .name = "orbment",
            .description = "Hook and input api.",
            .version = VERSION,
            .methods = methods,
         }
      };

      if (!register_plugin(&core, NULL))
         return false;
   }

   if (chck_cstr_is_empty(PLUGINS_PATH)) {
      plog(0, PLOG_ERROR, "Could not find plugins path. PLUGINS_PATH was not set during compile.");
      return true;
   }

   {
      struct chck_string xdg = {0};
      {
         char *tmp = xdg_get_path("XDG_DATA_HOME", ".local/share");
         chck_string_set_cstr(&xdg, tmp, true);
         free(tmp);
      }

      chck_string_set_format(&xdg, "%s/orbment/plugins", xdg.data);

#ifndef NDEBUG
      // allows running without install, as long as you build in debug mode
      const char *paths[] = { PLUGINS_PATH, "plugins", xdg.data, NULL };
#else
      const char *paths[] = { PLUGINS_PATH, xdg.data, NULL };
#endif

      // FIXME: add portable directory code to chck/fs/fs.c
      for (uint32_t i = 0; paths[i]; ++i) {
         DIR *d;
         if (!(d = opendir(paths[i]))) {
            plog(0, PLOG_WARN, "Could not open plugins directory: %s", paths[i]);
            continue;
         }

         struct dirent *dir;
         while ((dir = readdir(d))) {
            if (!chck_cstr_starts_with(dir->d_name, "orbment-plugin-"))
               continue;

            struct chck_string tmp = {0};
            if (chck_string_set_format(&tmp, "%s/%s", paths[i], dir->d_name))
               register_plugin_from_path(tmp.data);
            chck_string_release(&tmp);
         }

         closedir(d);
      }

      chck_string_release(&xdg);
   }

   load_plugins();
   return true;
}

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   static const struct wlc_interface interface = {
      .output = {
         .created = output_created,
         .destroyed = output_destroyed,
         .resolution = output_resolution,
      },

      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .focus = view_focus,
         .move_to_output = view_move_to_output,

         .request = {
            .geometry = view_geometry_request,
            .state = view_state_request,
         },
      },

      .pointer = {
         .button = pointer_button,
      },

      .keyboard = {
         .key = keyboard_key,
      },
   };

   // get before wlc_init, as it may set DISPLAY for xwayland
   const char *x11 = getenv("DISPLAY");

   if (!wlc_init(&interface, argc, argv))
      return EXIT_FAILURE;

   // default to alt on x11 session
   if (!chck_cstr_is_empty(x11))
      orbment.prefix = WLC_BIT_MOD_ALT;

   struct sigaction action = {
      .sa_handler = SIG_DFL,
      .sa_flags = SA_NOCLDWAIT
   };

   // do not care about childs
   sigaction(SIGCHLD, &action, NULL);

   if (!plugins_init())
      return EXIT_FAILURE;

   plog(0, PLOG_INFO, "orbment started");
   wlc_run();

   remove_hooks();
   remove_keybinds();
   deload_plugins();

   memset(&orbment, 0, sizeof(orbment));
   plog(0, PLOG_INFO, "-!- Orbment is gone, bye bye!");
   return EXIT_SUCCESS;
}
