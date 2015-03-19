#include <orbment/plugin.h>
#include <chck/math/math.h>
#include <chck/pool/pool.h>
#include <chck/string/string.h>
#include "common.h"
#include "config.h"

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(plugin_h, const char *name, const char **syntax, const struct function*, intptr_t arg);
static bool (*add_hook)(plugin_h, const char *name, const struct function*);

typedef void (*layout_fun_t)(const struct wlc_geometry *region, const wlc_handle *views, size_t memb);
struct layout {
   struct chck_string name;
   layout_fun_t function;
   plugin_h owner;
};

static struct {
   struct {
      // simplest data structure to cycle
      // there usually isn't many layouts so linear search is fast enough.
      // contigous arrays are very fast.
      struct chck_iter_pool pool;
      size_t index;
   } layouts;

   struct {
      const struct layout *layout;
   } active;

   plugin_h self;
} plugin;

static void
next_layout(size_t offset, enum direction dir)
{
   const size_t index = plugin.layouts.index, memb = plugin.layouts.pool.items.count;
   plugin.layouts.index = (dir == PREV ? chck_clampsz(index - offset, 0, memb - 1) : index + offset) % chck_maxsz(memb, 1);
   plugin.active.layout = chck_iter_pool_get(&plugin.layouts.pool, plugin.layouts.index);
}

static bool
layout_exists(const char *name)
{
   const struct layout *l;
   chck_iter_pool_for_each(&plugin.layouts.pool, l)
      if (chck_string_eq_cstr(&l->name, name))
         return true;
   return false;
}

static bool
add_layout(plugin_h caller, const char *name, const struct function *fun)
{
   if (!name || !fun || !caller)
      return false;

   static const char *signature = "v(*,h[],sz)|1";

   if (!chck_cstreq(fun->signature, signature)) {
      plog(plugin.self, PLOG_WARN, "Wrong signature provided for '%s layout' function. (%s != %s)", name, signature, fun->signature);
      return false;
   }

   if (layout_exists(name)) {
      plog(plugin.self, PLOG_WARN, "Layout with name '%s' already exists", name);
      return false;
   }

   if (!plugin.layouts.pool.items.member && !chck_iter_pool(&plugin.layouts.pool, 32, 0, sizeof(struct layout)))
      return false;

   struct layout l = {
      .function = fun->function,
      .owner = caller,
   };

   if (!chck_string_set_cstr(&l.name, name, true))
      return false;

   if (!chck_iter_pool_push_back(&plugin.layouts.pool, &l))
      goto error0;

   plog(plugin.self, PLOG_INFO, "Added layout: %s", name);

   if (!plugin.active.layout)
      next_layout(1, NEXT);

   return true;

error0:
   chck_string_release(&l.name);
   return false;
}

static void
layout_release(struct layout *layout)
{
   if (!layout)
      return;

   chck_string_release(&layout->name);
}

static void
remove_layout(plugin_h caller, const char *name)
{
   struct layout *l;
   chck_iter_pool_for_each(&plugin.layouts.pool, l) {
      if (l->owner != caller || !chck_string_eq_cstr(&l->name, name))
         continue;

      plog(plugin.self, PLOG_INFO, "Removed layout: %s", l->name.data);
      layout_release(l);
      chck_iter_pool_remove(&plugin.layouts.pool, _I - 1);

      if (plugin.layouts.index >= _I - 1)
         next_layout(1, PREV);

      break;
   }
}

static void
remove_layouts_for_plugin(plugin_h caller)
{
   struct layout *l;
   chck_iter_pool_for_each(&plugin.layouts.pool, l) {
      if (l->owner != caller)
         continue;

      plog(plugin.self, PLOG_INFO, "Removed layout: %s", l->name.data);
      layout_release(l);
      chck_iter_pool_remove(&plugin.layouts.pool, _I - 1);

      if (plugin.layouts.index >= _I - 1)
         next_layout(1, PREV);

      --_I;
   }
}

static void
remove_layouts(void)
{
   chck_iter_pool_for_each_call(&plugin.layouts.pool, layout_release);
   chck_iter_pool_release(&plugin.layouts.pool);
   plugin.layouts.index = 0;
   plugin.active.layout = NULL;
}

static void
layout_parent(wlc_handle view, wlc_handle parent, const struct wlc_size *size)
{
   assert(view && parent);

   // Size to fit the undermost parent
   // TODO: Use surface height as base instead of current
   wlc_handle under;
   for (under = parent; under && wlc_view_get_parent(under); under = wlc_view_get_parent(under));

   // Undermost view and parent view geometry
   const struct wlc_geometry *u = wlc_view_get_geometry(under);
   const struct wlc_geometry *p = wlc_view_get_geometry(parent);

   // Current constrained size
   const float cw = chck_maxf(size->w, u->size.w * 0.6);
   const float ch = chck_maxf(size->h, u->size.h * 0.6);

   struct wlc_geometry g;
   g.size.w = chck_minf(cw, u->size.w * 0.8);
   g.size.h = chck_minf(ch, u->size.h * 0.8);
   g.origin.x = p->size.w * 0.5 - g.size.w * 0.5;
   g.origin.y = p->size.h * 0.5 - g.size.h * 0.5;
   wlc_view_set_geometry(view, &g);
}

static void
relayout(wlc_handle output)
{
   const struct wlc_size *r;
   if (!(r = wlc_output_get_resolution(output)))
      return;

   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);
   for (size_t i = 0; i < memb; ++i) {
      if (wlc_output_get_mask(output) != wlc_view_get_mask(views[i]))
         continue;

      if (wlc_view_get_type(views[i]) & BIT_BEMENU) {
         struct wlc_geometry g = *wlc_view_get_geometry(views[i]);
         g.origin = (struct wlc_origin){ 0, 0 };
         wlc_view_set_geometry(views[i], &g);
      }

      if (wlc_view_get_state(views[i]) & WLC_BIT_FULLSCREEN)
         wlc_view_set_geometry(views[i], &(struct wlc_geometry){ { 0, 0 }, *r });

      if (wlc_view_get_type(views[i]) & WLC_BIT_SPLASH) {
         struct wlc_geometry g = *wlc_view_get_geometry(views[i]);
         g.origin = (struct wlc_origin){ r->w * 0.5 - g.size.w * 0.5, r->h * 0.5 - g.size.h * 0.5 };
         wlc_view_set_geometry(views[i], &g);
      }

      wlc_handle parent;
      if (is_managed(views[i]) && !is_or(views[i]) && (parent = wlc_view_get_parent(views[i])))
         layout_parent(views[i], parent, &wlc_view_get_geometry(views[i])->size);
   }

   if (plugin.active.layout) {
      struct chck_iter_pool tiled = {{0}};
      if (!chck_iter_pool(&tiled, memb, memb, sizeof(wlc_handle)))
         return;

      size_t memb;
      const wlc_handle *views = wlc_output_get_mutable_views(output, &memb);
      for (size_t i = 0; i < memb; ++i) {
         if (is_tiled(views[i]) && wlc_output_get_mask(output) == wlc_view_get_mask(views[i])) {
            wlc_view_set_state(views[i], WLC_BIT_MAXIMIZED, true);
            chck_iter_pool_push_back(&tiled, &views[i]);
         }
      }

      plugin.active.layout->function(&(struct wlc_geometry){ { 0, 0 }, *r }, tiled.items.buffer, tiled.items.count);
      chck_iter_pool_release(&tiled);
   }
}

static void
output_resolution(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to)
{
   (void)output, (void)from, (void)to;
   relayout(output);
}

static void
cycle_output(wlc_handle output, enum direction dir)
{
   size_t memb;
   wlc_handle *views = wlc_output_get_mutable_views(output, &memb);
   if (memb < 2)
      return;

   switch (dir) {
      case NEXT:
         {
            size_t last = NOTINDEX;
            for (size_t i = 0; i < memb; ++i) {
               if (!is_tiled(views[i]) || wlc_view_get_mask(views[i]) != wlc_output_get_mask(output))
                  continue;

               if (last != NOTINDEX) {
                  wlc_handle tmp = views[last];
                  views[last] = views[i];
                  views[i] = tmp;
               }

               last = i;
            }
         }
         break;

      case PREV:
         {
            size_t last = NOTINDEX;
            for (size_t i = memb; i > 0; --i) {
               if (!is_tiled(views[i - 1]) || wlc_view_get_mask(views[i - 1]) != wlc_output_get_mask(output))
                  continue;

               if (last != NOTINDEX) {
                  wlc_handle tmp = views[last];
                  views[last] = views[i - 1];
                  views[i - 1] = tmp;
               }

               last = i - 1;
            }
         }
         break;
   }
   relayout(output);
}

static void
view_geometry_request(wlc_handle view, const struct wlc_geometry *geometry)
{
   const uint32_t type = wlc_view_get_type(view);
   const uint32_t state = wlc_view_get_state(view);
   const bool tiled = is_tiled(view);
   const bool action = ((state & WLC_BIT_RESIZING) || (state & WLC_BIT_MOVING));

   if (tiled && !action)
      return;

   if (tiled)
      wlc_view_set_state(view, WLC_BIT_MAXIMIZED, false);

   if ((state & WLC_BIT_FULLSCREEN) || (type & WLC_BIT_SPLASH))
      return;

   wlc_handle parent;
   if (is_managed(view) && !is_or(view) && (parent = wlc_view_get_parent(view))) {
      layout_parent(view, parent, &geometry->size);
   } else {
      wlc_view_set_geometry(view, geometry);
   }
}

static void
view_state_request(wlc_handle view, const enum wlc_view_state_bit state, const bool toggle)
{
   wlc_view_set_state(view, state, toggle);

   switch (state) {
      case WLC_BIT_MAXIMIZED:
         if (toggle)
            relayout(wlc_view_get_output(view));
      break;
      case WLC_BIT_FULLSCREEN:
         relayout(wlc_view_get_output(view));
      break;
      default:break;
   }
}

static void
view_created_or_destroyed(wlc_handle view)
{
   relayout(wlc_view_get_output(view));
}

static void
key_cb_cycle_clients(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   cycle_output(wlc_get_focused_output(), NEXT);
}

static void
key_cb_next_layout(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   next_layout(1, NEXT);
   relayout(wlc_get_focused_output());
}

static void
plugin_deloaded(plugin_h ph)
{
   remove_layouts_for_plugin(ph);
}

static const struct {
   const char *name, **syntax;
   keybind_fun_t function;
} keybinds[] = {
   { "cycle clients", (const char*[]){ "<P-h>", NULL }, key_cb_cycle_clients },
   { "next layout", (const char*[]){ "<P-w>", NULL }, key_cb_next_layout },
   {0},
};

void
plugin_deinit(plugin_h self)
{
   (void)self;
   remove_layouts();
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   if (!(add_keybind = import_method(self, orbment, "add_keybind", "b(h,c[],c*[],fun,ip)|1")) ||
       !(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(self, keybinds[i].name, keybinds[i].syntax, FUN(keybinds[i].function, "v(h,u32,ip)|1"), 0))
         return false;

   return (add_hook(self, "plugin.deloaded", FUN(plugin_deloaded, "v(h)|1")) &&
           add_hook(self, "output.resolution", FUN(output_resolution, "v(h,*,*)|1")) &&
           add_hook(self, "view.created", FUN(view_created_or_destroyed, "v(h)|1")) &&
           add_hook(self, "view.destroyed", FUN(view_created_or_destroyed, "v(h)|1")) &&
           add_hook(self, "view.geometry_request", FUN(view_geometry_request, "v(h,*)|1")) &&
           add_hook(self, "view.state_request", FUN(view_state_request, "v(h,e,b)|1")));
}

const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(relayout, "v(h)|1"),
      REGISTER_METHOD(add_layout, "b(h,c[],fun)|1"),
      REGISTER_METHOD(remove_layout, "v(h,c[])|1"),
      {0},
   };

   static const struct plugin_info info = {
      .name = "layout",
      .description = "Layout api.",
      .version = VERSION,
      .methods = methods,
   };

   return &info;
}
