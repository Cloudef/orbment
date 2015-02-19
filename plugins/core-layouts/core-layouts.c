#include <loliwm/plugin.h>
#include <stdio.h>
#include <assert.h>

#include <wlc.h>
#include <wayland-util.h>

#include <chck/math/math.h>

static struct {
   struct {
      float cut;
   } nmaster;
} config = {
   .nmaster = {
      .cut = 0.5f,
   },
};

static bool (*is_tiled)(struct wlc_view *v);
static void (*relayout)(struct wlc_space *space);

typedef void (*layout_fun_t)(struct wlc_space*);
static bool (*add_layout)(const char *name, layout_fun_t);
static void (*remove_layout)(const char *name);

typedef void (*keybind_fun_t)(struct wlc_compositor*, struct wlc_view*, uint32_t time, intptr_t arg);
static bool (*add_keybind)(const char *name, const char *syntax, keybind_fun_t, intptr_t arg);
static void (*remove_keybind)(const char *name);

static void
key_cb_nmaster_grow(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   config.nmaster.cut = chck_minf(config.nmaster.cut + 0.01, 1.0);
   relayout(wlc_compositor_get_focused_space(compositor));
}

static void
key_cb_nmaster_shrink(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   config.nmaster.cut = chck_maxf(config.nmaster.cut - 0.01, 0.0);
   relayout(wlc_compositor_get_focused_space(compositor));
}

static void
nmaster(struct wlc_space *space)
{
   struct wl_list *views;
   if (!(views = wlc_space_get_userdata(space)))
      return;

   struct wlc_output *output = wlc_space_get_output(space);
   const struct wlc_size *resolution = wlc_output_get_resolution(output);

   struct wlc_view *v;
   uint32_t count = 0;
   wlc_view_for_each_user(v, views)
      if (is_tiled(v)) ++count;

   bool toggle = false;
   uint32_t y = 0, height = resolution->h / (count > 1 ? count - 1 : 1);
   uint32_t fheight = (resolution->h > height * (count - 1) ? height + (resolution->h - height * (count - 1)) : height);

   wlc_view_for_each_user(v, views) {
      if (!is_tiled(v))
         continue;

      uint32_t slave = resolution->w * config.nmaster.cut;
      wlc_view_set_state(v, WLC_BIT_MAXIMIZED, true);

      struct wlc_geometry g = {
         .origin = { (toggle ? resolution->w - slave : 0), y },
         .size = { (count > 1 ? (toggle ? slave : resolution->w - slave) : resolution->w), (toggle ? (y == 0 ? fheight : height) : resolution->h) },
      };

      wlc_view_set_geometry(v, &g);

      if (toggle)
         y += (y == 0 ? fheight : height);

      toggle = true;
   }
}

static const struct {
   const char *name, *syntax;
   keybind_fun_t function;
} keybinds[] = {
   { "grow nmaster", "<P-i>", key_cb_nmaster_grow },
   { "shrink nmaster", "<P-o>", key_cb_nmaster_shrink },
   {0},
};

static const struct {
   const char *name;
   layout_fun_t function;
} layouts[] = {
   { "nmaster", nmaster },
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
   plugin_h loliwm;
   if (!(loliwm = import_plugin("loliwm")))
      return false;

   if (!has_methods(loliwm,
            (const struct method_info[]){
               METHOD("is_tiled", "b(p)|1"),
               METHOD("add_layout", "b(c[],p)|1"),
               METHOD("remove_layout", "v(c[])|1"),
               METHOD("add_keybind", "b(c[],c[],p,ip)|1"),
               METHOD("remove_keybind", "v(c[])|1"),
               METHOD("relayout", "v(p)|1"),
               {0},
            }))
      return false;

   is_tiled = import_method(loliwm, "is_tiled", "b(p)|1");
   relayout = import_method(loliwm, "relayout", "v(p)|1");
   add_layout = import_method(loliwm, "add_layout", "b(c[],p)|1");
   remove_layout = import_method(loliwm, "remove_layout", "v(c[])|1");
   add_keybind = import_method(loliwm, "add_keybind", "b(c[],c[],p,ip)|1");
   remove_keybind = import_method(loliwm, "remove_keybind", "v(c[])|1");

   for (size_t i = 0; layouts[i].name; ++i)
      if (!add_layout(layouts[i].name, layouts[i].function))
         return false;

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(keybinds[i].name, keybinds[i].syntax, keybinds[i].function, 0))
         return false;

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const struct plugin_info info = {
      .name = "core-layouts",
      .version = "1.0.0",
   };

   return &info;
}
