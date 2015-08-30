#include <wlc/wlc.h>
#include <libinput.h>
#include <chck/string/string.h>
#include <orbment/plugin.h>
#include "config.h"

static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   plugin_h self;
} plugin;

static void
configure_device(struct libinput_device *device)
{
   const uint32_t id_product = libinput_device_get_id_product(device);
   const uint32_t id_vendor = libinput_device_get_id_vendor(device);
   if (id_product == 1 && id_vendor == 0)
      return; // power button

   plugin_h configuration;
   bool (*configuration_get)(const char *key, const char type, void *value_out);
   if (!(configuration = import_plugin(plugin.self, "configuration")) ||
       !(configuration_get = import_method(plugin.self, configuration, "get", "b(c[],c,v)|1")))
      return;

   const char *name = libinput_device_get_name(device);
   const char *sysname = libinput_device_get_sysname(device);
   plog(plugin.self, PLOG_INFO, "Configuring input device: %s (%s) (%u-%u)", name, sysname, id_product, id_vendor);

   struct chck_string str = {0}, id = {0};
   if (!chck_string_set_format(&id, "input-%u-%u", id_product, id_vendor))
      return;

   {
      bool v;
      if (chck_string_set_format(&str, "/%s/tap-to-click", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_tap_set_enabled(device, (v ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED));

      if (chck_string_set_format(&str, "/%s/drag-lock", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_tap_set_drag_lock_enabled(device, (v ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED));

      if (chck_string_set_format(&str, "/%s/natural-scroll", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_scroll_set_natural_scroll_enabled(device, v);

      if (chck_string_set_format(&str, "/%s/left-handed", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_left_handed_set(device, v);

      if (chck_string_set_format(&str, "/%s/emulate-middle", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_middle_emulation_set_enabled(device, (v ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED));

      if (chck_string_set_format(&str, "/%s/disable-while-typing", id.data) && configuration_get(str.data, 'b', &v))
         libinput_device_config_dwt_set_enabled(device, (v ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED));
   }

   {
      const char *v;
      if (chck_string_set_format(&str, "/%s/disabled", id.data) && configuration_get(str.data, 's', &v)) {
         libinput_device_config_send_events_set_mode(device,
               (chck_cstreq(v, "always") ? LIBINPUT_CONFIG_SEND_EVENTS_DISABLED :
               (chck_cstreq(v, "on-external-device") ? LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE :
                LIBINPUT_CONFIG_SEND_EVENTS_ENABLED)));
      }

      if (chck_string_set_format(&str, "/%s/click-method", id.data) && configuration_get(str.data, 's', &v)) {
         libinput_device_config_click_set_method(device,
               (chck_cstreq(v, "finger") ? LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER :
               (chck_cstreq(v, "button-areas") ? LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS :
                LIBINPUT_CONFIG_CLICK_METHOD_NONE)));
      }

      if (chck_string_set_format(&str, "/%s/scroll-method", id.data) && configuration_get(str.data, 's', &v)) {
         libinput_device_config_scroll_set_method(device,
               (chck_cstreq(v, "two-fingers") ? LIBINPUT_CONFIG_SCROLL_2FG :
               (chck_cstreq(v, "edge") ? LIBINPUT_CONFIG_SCROLL_EDGE :
               (chck_cstreq(v, "button") ? LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN :
                LIBINPUT_CONFIG_SCROLL_NO_SCROLL))));
      }
   }

   {
      uint32_t v;
      if (chck_string_set_format(&str, "/%s/scroll-button", id.data) && configuration_get(str.data, 'u', &v))
         libinput_device_config_scroll_set_button(device, v);
   }

   {
      double v;
      if (chck_string_set_format(&str, "/%s/accel", id.data) && configuration_get(str.data, 'd', &v)) {
         if (v <= 1 && v >= -1) {
            libinput_device_config_accel_set_speed(device, v);
         } else {
            plog(plugin.self, PLOG_WARN, "Accel must be normalized range [-1, 1]");
         }
      }
   }
}

static bool
input_created(struct libinput_device *device)
{
   configure_device(device);
   return true;
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

bool
plugin_init(plugin_h self)
{
   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   plugin.self = self;
   return add_hook(self, "input.created", FUN(input_created, "b(*)|1"));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-input",
      .description = "Input device configuration.",
      .version = VERSION,
      .requires = requires,
   };

   return &info;
}
