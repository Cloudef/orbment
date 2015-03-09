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

static const char *layout_signature = "v(*,h[],sz)|1";
typedef void (*layout_fun_t)(const struct wlc_geometry *r, const wlc_handle *views, size_t memb);
static bool (*add_layout)(const char *name, const struct function*);
static void (*remove_layout)(const char *name);

static const char *keybind_signature = "v(h,u32,ip)|1";
typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(const char *name, const char **syntax, const struct function*, intptr_t arg);
static void (*remove_keybind)(const char *name);

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
plugin_deinit(void)
{
   for (size_t i = 0; layouts[i].name; ++i)
      remove_layout(layouts[i].name);

   for (size_t i = 0; keybinds[i].name; ++i)
      remove_keybind(keybinds[i].name);

   return true;
}

bool
plugin_init(void)
{
   plugin_h orbment;
   if (!(orbment = import_plugin("orbment")))
      return false;

   if (!has_methods(orbment,
            (const struct method_info[]){
               METHOD("add_layout", "b(c[],fun)|1"),
               METHOD("remove_layout", "v(c[])|1"),
               METHOD("add_keybind", "b(c[],c*[],fun,ip)|1"),
               METHOD("remove_keybind", "v(c[])|1"),
               METHOD("relayout", "v(h)|1"),
               {0},
            }))
      return false;

   relayout = import_method(orbment, "relayout", "v(h)|1");
   add_layout = import_method(orbment, "add_layout", "b(c[],fun)|1");
   remove_layout = import_method(orbment, "remove_layout", "v(c[])|1");
   add_keybind = import_method(orbment, "add_keybind", "b(c[],c*[],fun,ip)|1");
   remove_keybind = import_method(orbment, "remove_keybind", "v(c[])|1");

   for (size_t i = 0; layouts[i].name; ++i)
      if (!add_layout(layouts[i].name, FUN(layouts[i].function, layout_signature)))
         return false;

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(keybinds[i].name, keybinds[i].syntax, FUN(keybinds[i].function, keybind_signature), 0))
         return false;

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const struct plugin_info info = {
      .name = "core-layouts",
      .description = "Provides core set of layouts.",
      .version = VERSION,
   };

   return &info;
}
