#include <orbment/plugin.h>
#include <chck/buffer/buffer.h>
#include "config.h"
#include <wlc/wlc.h>
#include <wlc/wlc-render.h>

static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   plugin_h self;
   struct chck_buffer fb;
} plugin;

static void
view_pre_render(wlc_handle view)
{
   const size_t bsz = 2; // 2px
   struct wlc_geometry g = *wlc_view_get_geometry(view); // dereference here should never crash
   g.origin.x -= bsz;
   g.origin.y -= bsz;
   g.size.w += bsz * 2;
   g.size.h += bsz * 2;

   const size_t vsz = g.size.w * g.size.h * 4;
   if (plugin.fb.size < vsz && !chck_buffer_resize(&plugin.fb, vsz)) {
      return;
   } else {
      memset(plugin.fb.buffer, 255, vsz); // white
   }

   // LALALA I DON'T CARE ABOUT TRANSPARENT WINDOWS, CAN'T HEAR YOU LALALA
   wlc_pixels_write(WLC_RGBA8888, &g, plugin.fb.buffer);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   chck_buffer_release(&plugin.fb);
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

   if (!add_hook(self, "view.pre_render", FUN(view_pre_render, "v(*)|1")))
      return false;

   if (!chck_buffer(&plugin.fb, 0, chck_endianess()))
      return false;

   return true;
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const struct plugin_info info = {
      .name = "crappy-borders",
      .description = "Provides crappy window borders.",
      .version = VERSION,
   };

   return &info;
}
