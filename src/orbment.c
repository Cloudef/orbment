#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <dirent.h>
#include <wlc/wlc.h>
#include <chck/pool/pool.h>
#include <chck/xdg/xdg.h>
#include "plugin.h"
#include "common.h"
#include "config.h"

enum hook_type {
   HOOK_PLUGIN_LOADED,
   HOOK_PLUGIN_DELOADED,
   HOOK_OUTPUT_CREATED,
   HOOK_OUTPUT_DESTROYED,
   HOOK_OUTPUT_FOCUS,
   HOOK_OUTPUT_RESOLUTION,
   HOOK_VIEW_CREATED,
   HOOK_VIEW_DESTROYED,
   HOOK_VIEW_FOCUS,
   HOOK_VIEW_MOVE_TO_OUTPUT,
   HOOK_VIEW_GEOMETRY_REQUEST,
   HOOK_VIEW_STATE_REQUEST,
   HOOK_KEYBOARD_KEY,
   HOOK_POINTER_BUTTON,
   HOOK_POINTER_SCROLL,
   HOOK_POINTER_MOTION,
   HOOK_TOUCH_TOUCH,
   HOOK_COMPOSITOR_READY,
   HOOK_LAST,
};

struct hook {
   void *function;
   plugin_h owner;
};

static struct {
   struct chck_iter_pool hooks[HOOK_LAST];
} orbment;

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
plugin_loaded(const struct plugin *plugin)
{
   assert(plugin);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_PLUGIN_LOADED], hook) {
      void (*fun)() = hook->function;
      fun(plugin->handle + 1);
   }
}

static void
plugin_deloaded(const struct plugin *plugin)
{
   assert(plugin);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_PLUGIN_DELOADED], hook) {
      void (*fun)() = hook->function;
      fun(plugin->handle + 1);
   }

   remove_hooks_for_plugin(plugin->handle + 1);
}

static bool
output_created(wlc_handle output)
{
   bool created = true;
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_CREATED], hook) {
      bool (*fun)() = hook->function;
      if (!fun(output))
         created = false;
   }
   return created;
}

static void
output_destroyed(wlc_handle output)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_DESTROYED], hook) {
      void (*fun)() = hook->function;
      fun(output);
   }
}

static void
output_focus(wlc_handle output, bool focus)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_FOCUS], hook) {
      void (*fun)() = hook->function;
      fun(output, focus);
   }
}

static void
output_resolution(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_OUTPUT_RESOLUTION], hook) {
      void (*fun)() = hook->function;
      fun(output, from, to);
   }
}

static bool
view_created(wlc_handle view)
{
   plog(0, PLOG_INFO, "new view: %zu (%zu)", view, wlc_view_get_parent(view));

   bool created = true;
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_CREATED], hook) {
      bool (*fun)() = hook->function;
      if (!fun(view))
         created = false;
   }
   return created;
}

static void
view_destroyed(wlc_handle view)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_DESTROYED], hook) {
      void (*fun)() = hook->function;
      fun(view);
   }

   plog(0, PLOG_INFO, "view destroyed: %zu", view);
}

static void
view_focus(wlc_handle view, bool focus)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_FOCUS], hook) {
      void (*fun)() = hook->function;
      fun(view, focus);
   }
}

static void
view_move_to_output(wlc_handle view, wlc_handle from, wlc_handle to)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_MOVE_TO_OUTPUT], hook) {
      void (*fun)() = hook->function;
      fun(view, from, to);
   }
}

static void
view_geometry_request(wlc_handle view, const struct wlc_geometry *geometry)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_GEOMETRY_REQUEST], hook) {
      void (*fun)() = hook->function;
      fun(view, geometry);
   }
}

static void
view_state_request(wlc_handle view, const enum wlc_view_state_bit state, const bool toggle)
{
   plog(0, PLOG_INFO, "STATE: %d (%d)", state, toggle);

   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_VIEW_STATE_REQUEST], hook) {
      void (*fun)() = hook->function;
      fun(view, state, toggle);
   }
}

static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
{
   struct hook *hook;
   bool handled = false;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_KEYBOARD_KEY], hook) {
      bool (*fun)() = hook->function;
      if (fun(view, time, modifiers, key, sym, state))
         handled = true;
   }
   return handled;
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state)
{
   struct hook *hook;
   bool handled = false;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_POINTER_BUTTON], hook) {
      bool (*fun)() = hook->function;
      if (fun(view, time, modifiers, button, state))
         handled = true;
   }
   return handled;
}

static bool
pointer_scroll(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint8_t axis_bits, double amount[2])
{
   struct hook *hook;
   bool handled = false;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_POINTER_SCROLL], hook) {
      bool (*fun)() = hook->function;
      if (fun(view, time, modifiers, axis_bits, amount))
         handled = true;
   }
   return handled;
}

static bool
pointer_motion(wlc_handle view, uint32_t time, const struct wlc_origin *motion)
{
   struct hook *hook;
   bool handled = false;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_POINTER_MOTION], hook) {
      bool (*fun)() = hook->function;
      if (fun(view, time, motion))
         handled = true;
   }
   return handled;
}

static bool
touch_touch(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, enum wlc_touch_type type, int32_t slot, const struct wlc_origin *touch)
{
   struct hook *hook;
   bool handled = false;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_TOUCH_TOUCH], hook) {
      bool (*fun)() = hook->function;
      if (fun(view, time, modifiers, type, slot, touch))
         handled = true;
   }
   return handled;
}

static void
compositor_ready(void)
{
   struct hook *hook;
   chck_iter_pool_for_each(&orbment.hooks[HOOK_COMPOSITOR_READY], hook) {
      void (*fun)() = hook->function;
      fun();
   }
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
      { "output.focus", HOOK_OUTPUT_FOCUS },
      { "output.resolution", HOOK_OUTPUT_RESOLUTION },
      { "view.created", HOOK_VIEW_CREATED },
      { "view.destroyed", HOOK_VIEW_DESTROYED },
      { "view.focus", HOOK_VIEW_FOCUS },
      { "view.move_to_output", HOOK_VIEW_MOVE_TO_OUTPUT },
      { "view.geometry_request", HOOK_VIEW_GEOMETRY_REQUEST },
      { "view.state_request", HOOK_VIEW_STATE_REQUEST },
      { "keyboard.key", HOOK_KEYBOARD_KEY },
      { "pointer.button", HOOK_POINTER_BUTTON },
      { "pointer.scroll", HOOK_POINTER_SCROLL },
      { "pointer.motion", HOOK_POINTER_MOTION },
      { "touch.touch", HOOK_TOUCH_TOUCH },
      { "compositor.ready", HOOK_COMPOSITOR_READY },
      { NULL, HOOK_LAST },
   };

   for (uint32_t i = 0; map[i].name; ++i) {
      if (chck_cstreq(type, map[i].name))
         return map[i].type;
   }

   return HOOK_LAST;
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
      "b(h)|1", // HOOK_OUTPUT_CREATED
      "v(h)|1", // HOOK_OUTPUT_DESTROYED
      "v(h,b)|1", // HOOK_OUTPUT_FOCUS
      "v(h,*,*)|1", // HOOK_OUTPUT_RESOLUTION
      "b(h)|1", // HOOK_VIEW_CREATED
      "v(h)|1", // HOOK_VIEW_DESTROYED
      "v(h,b)|1", // HOOK_VIEW_FOCUS
      "v(h,h,h)|1", // HOOK_VIEW_MOVE_TO_OUTPUT
      "v(h,*)|1", // HOOK_VIEW_GEOMETRY_REQUEST
      "v(h,e,b)|1", // HOOK_VIEW_STATE_REQUEST
      "b(h,u32,*,u32,u32,e)|1", // HOOK_KEYBOARD_KEY
      "b(h,u32,*,u32,e)|1", // HOOK_POINTER_BUTTON
      "b(h,u32,*,u8,d[2])|1", // HOOK_POINTER_SCROLL
      "b(h,u32,*)|1", // HOOK_POINTER_MOTION
      "b(h,u32,*,e,i32,*)|1", // HOOK_TOUCH_TOUCH
      "v(v)|1", // HOOK_COMPOSITOR_READY
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

static bool
plugins_init(void)
{
   set_plugin_callbacks(plugin_loaded, plugin_deloaded);

   {
      static const struct method methods[] = {
         REGISTER_METHOD(add_hook, "b(h,c[],fun)|1"),
         REGISTER_METHOD(remove_hook, "v(h,c[])|1"),
         {0},
      };

      struct plugin core = {
         .info = {
            .name = "orbment",
            .description = "Hook api.",
            .version = VERSION,
            .methods = methods,
         },
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

static void
sigterm(int signal)
{
   (void)signal;
   wlc_log(WLC_LOG_INFO, "Got %s", (signal == SIGTERM ? "SIGTERM" : "SIGINT"));
   wlc_terminate();
}

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   static const struct wlc_interface interface = {
      .output = {
         .created = output_created,
         .destroyed = output_destroyed,
         .focus = output_focus,
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

      .keyboard = {
         .key = keyboard_key,
      },

      .pointer = {
         .button = pointer_button,
         .motion = pointer_motion,
         .scroll = pointer_scroll,
      },

      .touch = {
         .touch = touch_touch,
      },

      .compositor = {
         .ready = compositor_ready,
      },
   };

   if (!wlc_init(&interface, argc, argv))
      return EXIT_FAILURE;

   {
      struct sigaction action = {
         .sa_handler = SIG_DFL,
         .sa_flags = SA_NOCLDWAIT
      };

      // do not care about childs
      sigaction(SIGCHLD, &action, NULL);
   }

   {
      struct sigaction action = {
         .sa_handler = sigterm,
      };

      sigaction(SIGTERM, &action, NULL);
      sigaction(SIGINT, &action, NULL);
   }

   if (!plugins_init())
      return EXIT_FAILURE;

   plog(0, PLOG_INFO, "orbment started");
   wlc_run();

   remove_hooks();
   deload_plugins();

   memset(&orbment, 0, sizeof(orbment));
   plog(0, PLOG_INFO, "-!- Orbment is gone, bye bye!");
   return EXIT_SUCCESS;
}
