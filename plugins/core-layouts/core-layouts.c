#include <stdio.h>
#include <assert.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>
#include "config.h"

static struct {
   struct {
      float cut;
   } nmaster;
} config = {
   .nmaster = {
      .cut = 0.5f,
   },
};

static void (*relayout)(wlc_handle output);

typedef void (*layout_fun_t)(const struct wlc_geometry *r, const wlc_handle *views, size_t memb);
static bool (*add_layout)(plugin_h, const char *name, const struct function*);

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(plugin_h, const char *name, const char **syntax, const struct function*, intptr_t arg);

static void
key_cb_nmaster_grow(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   config.nmaster.cut = chck_minf(config.nmaster.cut + 0.01, 1.0);
   relayout(wlc_get_focused_output());
}

static void
key_cb_nmaster_shrink(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   config.nmaster.cut = chck_maxf(config.nmaster.cut - 0.01, 0.0);
   relayout(wlc_get_focused_output());
}

static void
nmaster(const struct wlc_geometry *r, const wlc_handle *views, size_t memb)
{
   bool toggle = false;
   uint32_t y = 0, height = r->size.h / (memb > 1 ? memb - 1 : 1);
   uint32_t fheight = (r->size.h > height * (memb - 1) ? height + (r->size.h - height * (memb - 1)) : height);

   for (size_t i = 0; i < memb; ++i) {
      uint32_t slave = r->size.w * config.nmaster.cut;
      wlc_view_set_state(views[i], WLC_BIT_MAXIMIZED, true);

      struct wlc_geometry g = {
         .origin = { r->origin.x + (toggle ? r->size.w - slave : 0), r->origin.y + y },
         .size = { (memb > 1 ? (toggle ? slave : r->size.w - slave) : r->size.w), (toggle ? (y == 0 ? fheight : height) : r->size.h) },
      };

      wlc_view_set_geometry(views[i], &g);

      if (toggle)
         y += (y == 0 ? fheight : height);

      toggle = true;
   }
}

static void
grid(const struct wlc_geometry *r, const wlc_handle *views, size_t memb)
{
   bool toggle = false;
   uint32_t y = 0;
   uint32_t w = r->size.w / 2, h = r->size.h / chck_maxu32((1 + memb) / 2, 1);
   for (size_t i = 0; i < memb; ++i) {
      struct wlc_geometry g = { { r->origin.x + (toggle ? w : 0), r->origin.y + y }, { (!toggle && i == memb - 1 ? r->size.w : w), h } };
      wlc_view_set_geometry(views[i], &g);
      y = y + (!(toggle = !toggle) ? h : 0);
   }
}

static void
monocle(const struct wlc_geometry *r, const wlc_handle *views, size_t memb)
{
   for (size_t i = 0; i < memb; ++i)
      wlc_view_set_geometry(views[i], r);
}

static const struct {
   const char *name, **syntax;
   keybind_fun_t function;
} keybinds[] = {
   { "grow nmaster", (const char*[]){ "<P-i>", NULL }, key_cb_nmaster_grow },
   { "shrink nmaster", (const char*[]){ "<P-o>", NULL }, key_cb_nmaster_shrink },
   {0},
};

static const struct {
   const char *name;
   layout_fun_t function;
} layouts[] = {
   { "nmaster", nmaster },
   { "grid", grid },
   { "monocle", monocle },
   {0},
};

bool
plugin_init(plugin_h self)
{
   plugin_h orbment, layout;
   if (!(orbment = import_plugin(self, "orbment")) || !(layout = import_plugin(self, "layout")))
      return false;

   if (!(add_keybind = import_method(self, orbment, "add_keybind", "b(h,c[],c*[],fun,ip)|1")))
      return false;

   if (!(relayout = import_method(self, layout, "relayout", "v(h)|1")) ||
       !(add_layout = import_method(self, layout, "add_layout", "b(h,c[],fun)|1")))
      return false;

   for (size_t i = 0; layouts[i].name; ++i)
      if (!add_layout(self, layouts[i].name, FUN(layouts[i].function, "v(*,h[],sz)|1")))
         return false;

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(self, keybinds[i].name, keybinds[i].syntax, FUN(keybinds[i].function, "v(h,u32,ip)|1"), 0))
         return false;

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "layout",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-layouts",
      .description = "Core set of layouts.",
      .version = VERSION,
      .requires = requires,
   };

   return &info;
}
