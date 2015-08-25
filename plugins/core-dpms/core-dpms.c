#include <unistd.h>
#include <stdlib.h>
#include <orbment/plugin.h>
#include <chck/string/string.h>
#include "config.h"
#include <wlc/wlc.h>

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(plugin_h, const char *name, const char **syntax, const struct function*, intptr_t arg);
static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   struct {
      struct wlc_event_source *sleep;
   } timers;

   // Sleep delay in seconds
   uint32_t delay;

   // Force sleep from keybind
   // Skips next activity event
   bool force;

   plugin_h self;
} plugin;

static bool
contains_fullscreen_view(wlc_handle output)
{
   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);
   for (size_t i = 0; i < memb; ++i) {
      if (wlc_view_get_state(views[i]) & WLC_BIT_FULLSCREEN)
         return true;
   }

   return false;
}

static int
timer_cb_sleep(void *arg)
{
   (void)arg;

   size_t memb;
   const wlc_handle *outputs = wlc_get_outputs(&memb);

   if (!plugin.force) {
      for (size_t i = 0; i < memb; ++i) {
         if (contains_fullscreen_view(outputs[i]))
            goto restart;
      }
   }

   plog(plugin.self, PLOG_INFO, "Going to sleep");

   for (size_t i = 0; i < memb; ++i)
      wlc_output_set_sleep(outputs[i], true);

   plugin.force = false;
   return 1;

restart:
   plog(plugin.self, PLOG_INFO, "Preventing sleep");
   wlc_event_source_timer_update(plugin.timers.sleep, 1000 * plugin.delay);
   return 1;
}

static void
key_cb_toggle_sleep(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg, (void)view;
   plugin.force = wlc_event_source_timer_update(plugin.timers.sleep, 1);
}

static bool
handle_activity(bool pressed)
{
   if (!pressed)
      return false;

   size_t memb;
   bool was_sleeping = false;
   const wlc_handle *outputs = wlc_get_outputs(&memb);
   for (size_t i = 0; i < memb; ++i) {
      if (wlc_output_get_sleep(outputs[i])) {
         wlc_output_set_sleep(outputs[i], false);
         was_sleeping = true;
      }
   }

   if (was_sleeping)
      plog(plugin.self, PLOG_INFO, "Woke up");

   if (!plugin.force)
      wlc_event_source_timer_update(plugin.timers.sleep, 1000 * plugin.delay);

   return was_sleeping;
}

/**
 * Handle keyboard.key separately, to pass key state information.
 */
static bool
keyboard_key(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, enum wlc_key_state state)
{
   (void)view, (void)time, (void)modifiers, (void)key;
   return handle_activity(state == WLC_KEY_STATE_PRESSED);
}

/**
 * Handle pointer.button separately, to pass button state information.
 */
static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state)
{
   (void)view, (void)time, (void)modifiers, (void)button;
   return handle_activity(state == WLC_BUTTON_STATE_PRESSED);
}

/**
 * Variadic () function.
 * All hooks have same return value (bool) so this is valid.
 */
static bool
activity()
{
   return handle_activity(true);
}

static const struct {
   const char *name, **syntax;
   keybind_fun_t function;
   intptr_t arg;
} keybinds[] = {
   { "Go to sleep", (const char*[]){ "<P-d>", NULL }, key_cb_toggle_sleep, 0 },
   {0},
};

static uint32_t
get_dpms_delay(plugin_h self)
{
   plugin_h configuration;
   bool (*configuration_get)(const char *key, const char type, void *value_out);
   if (!(configuration = import_plugin(self, "configuration")) ||
       !(configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1")))
      goto default_delay;

   const char *config;
   if (!configuration_get("/dpms/delay", 's', &config))
      goto default_delay;

   uint32_t delay;
   if (!chck_cstr_to_u32(config, &delay))
      goto default_delay;

   return delay;

default_delay:
   return 60 * 5; // 5 mins
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;

   if (plugin.timers.sleep)
      wlc_event_source_remove(plugin.timers.sleep);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   if (!(plugin.timers.sleep = wlc_event_loop_add_timer(timer_cb_sleep, NULL)))
      return false;

   plugin_h orbment, keybind;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(keybind = import_plugin(self, "keybind")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")) ||
       !(add_keybind = import_method(self, keybind, "add_keybind", "b(h,c[],c*[],fun,ip)|1")))
      return false;

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(self, keybinds[i].name, keybinds[i].syntax, FUN(keybinds[i].function, "v(h,u32,ip)|1"), keybinds[i].arg))
         return false;

   if (!add_hook(self, "keyboard.key", FUN(keyboard_key, "b(h,u32,*,u32,e)|1")) ||
       !add_hook(self, "pointer.button", FUN(pointer_button, "b(h,u32,*,u32,e,*)|1")) ||
       !add_hook(self, "pointer.scroll", FUN(activity, "b(h,u32,*,u8,d[2])|1")) ||
       !add_hook(self, "pointer.motion", FUN(activity, "b(h,u32,*)|1")) ||
       !add_hook(self, "touch.touch", FUN(activity, "b(h,u32,*,e,i32,*)|1")))
      return false;

   plugin.delay = get_dpms_delay(self);
   return wlc_event_source_timer_update(plugin.timers.sleep, 1000 * plugin.delay);
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "keybind",
      NULL,
   };

   static const char *after[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-dpsm",
      .description = "Core display power management.",
      .version = VERSION,
      .requires = requires,
      .after = after,
   };

   return &info;
}
