#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <wlc.h>
#include <wayland-util.h>

#include "config.h"
#include "plugin.h"

#include <xkbcommon/xkbcommon.h>

#include "chck/pool/pool.h"
#include "chck/lut/lut.h"

#define DEFAULT_TERMINAL "weston-terminal"
#define DEFAULT_MENU "bemenu-run"

// XXX: hack
enum {
   BIT_BEMENU = 1<<5,
};

typedef void (*layout_fun_t)(struct wlc_space*);
struct layout {
   const char *name;
   layout_fun_t function;
};

typedef void (*keybind_fun_t)(struct wlc_compositor*, struct wlc_view*, uint32_t time, intptr_t arg);
struct keybind {
   const char *name;
   struct chck_string syntax;
   keybind_fun_t function;
   intptr_t arg;
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
      struct chck_pool pool;
      struct chck_hash_table table;
   } keybinds;

   struct {
      const struct layout *layout;
      struct wlc_view *view;
   } active;

   uint32_t prefix;
   struct chck_string terminal;
} loliwm = {
   .prefix = WLC_BIT_MOD_ALT,
};

static void
next_or_prev_layout(bool direction)
{
   loliwm.layouts.index = (loliwm.layouts.index + (direction ? 1 : -1)) % loliwm.layouts.pool.items.count;
   loliwm.active.layout = chck_iter_pool_get(&loliwm.layouts.pool, loliwm.layouts.index);
}

static bool
layout_exists(const char *name)
{
   const struct layout *l;
   chck_iter_pool_for_each(&loliwm.layouts.pool, l)
      if (chck_cstreq(name, l->name))
         return true;
   return false;
}

static bool
add_layout(const char *name, layout_fun_t function)
{
   if (!name)
      return false;

   if (layout_exists(name)) {
      wlc_log(WLC_LOG_WARN, "Layout with name '%s' already exists", name);
      return false;
   }

   struct layout l = {
      .name = name,
      .function = function,
   };

   if (!chck_iter_pool_push_back(&loliwm.layouts.pool, &l))
      return false;

   wlc_log(WLC_LOG_INFO, "Added layout: %s", name);

   if (!loliwm.active.layout)
      next_or_prev_layout(true);

   return true;
}

static void
remove_layout(const char *name)
{
   const struct layout *l;
   chck_iter_pool_for_each(&loliwm.layouts.pool, l) {
      if (!chck_cstreq(name, l->name))
         continue;

      chck_iter_pool_remove(&loliwm.layouts.pool, _I - 1);
      wlc_log(WLC_LOG_INFO, "Removed layout: %s", name);

      if (loliwm.layouts.index >= _I - 1)
         next_or_prev_layout(false);

      break;
   }
}

static bool
keybind_exists(const char *name)
{
   const struct keybind *k;
   chck_pool_for_each(&loliwm.keybinds.pool, k)
      if (chck_cstreq(name, k->name))
         return true;
   return false;
}

static const struct keybind*
keybind_for_syntax(const char *syntax)
{
   size_t *index;
   if (!(index = chck_hash_table_str_get(&loliwm.keybinds.table, syntax, strlen(syntax))) || *index == (size_t)-1)
      return NULL;

   return chck_pool_get(&loliwm.keybinds.pool, *index);
}

static bool
add_keybind(const char *name, const char *syntax, keybind_fun_t function, intptr_t arg)
{
   if (!name)
      return false;

   if (keybind_exists(name)) {
      wlc_log(WLC_LOG_WARN, "Keybind with name '%s' already exists", name);
      return false;
   }

   struct keybind k = {
      .name = name,
      .function = function,
      .arg = arg,
   };

   chck_string_set_cstr(&k.syntax, syntax, false);

   size_t index;
   if (!chck_pool_add(&loliwm.keybinds.pool, &k, &index))
      return false;

   const struct keybind *o;
   if (!(o = keybind_for_syntax(k.syntax.data))) {
      chck_hash_table_str_set(&loliwm.keybinds.table, k.syntax.data, k.syntax.size, &index);
   } else {
      wlc_log(WLC_LOG_WARN, "'%s' is already mapped to keybind '%s'", syntax, o->name);
   }

   wlc_log(WLC_LOG_INFO, "Added keybind: %s (%s)", name, syntax);
   return true;
}

static void
remove_keybind(const char *name)
{
   const struct keybind *k;
   chck_pool_for_each(&loliwm.keybinds.pool, k) {
      if (!chck_cstreq(name, k->name))
         continue;

      chck_hash_table_str_set(&loliwm.keybinds.table, k->syntax.data, k->syntax.size, NULL);
      wlc_log(WLC_LOG_INFO, "Removed keybind: %s", name);
      break;
   };
}

static void
layout_parent(struct wlc_view *view, struct wlc_view *parent, const struct wlc_size *size)
{
   assert(view && parent);

   // Size to fit the undermost parent
   // TODO: Use surface height as base instead of current
   struct wlc_view *under;
   for (under = parent; under && wlc_view_get_parent(under); under = wlc_view_get_parent(under));

   // Undermost view and parent view geometry
   const struct wlc_geometry *u = wlc_view_get_geometry(under);
   const struct wlc_geometry *p = wlc_view_get_geometry(parent);

   // Current constrained size
   float cw = fmax(size->w, u->size.w * 0.6);
   float ch = fmax(size->h, u->size.h * 0.6);

   struct wlc_geometry g;
   g.size.w = fmin(cw, u->size.w * 0.8);
   g.size.h = fmin(ch, u->size.h * 0.8);
   g.origin.x = p->size.w * 0.5 - g.size.w * 0.5;
   g.origin.y = p->size.h * 0.5 - g.size.h * 0.5;
   wlc_view_set_geometry(view, &g);
}

static bool
should_focus_on_create(struct wlc_view *view)
{
   // Do not allow unmanaged views to steal focus (tooltips, dnds, etc..)
   // Do not allow parented windows to steal focus, if current window wasn't parent.
   uint32_t type = wlc_view_get_type(view);
   struct wlc_view *parent = wlc_view_get_parent(view);
   return (!(type & WLC_BIT_UNMANAGED) && (!loliwm.active.view || !parent || parent == loliwm.active.view));
}

static bool
is_or(struct wlc_view *view)
{
   return (wlc_view_get_type(view) & WLC_BIT_OVERRIDE_REDIRECT) || (wlc_view_get_state(view) & BIT_BEMENU);
}

static bool
is_managed(struct wlc_view *view)
{
   uint32_t type = wlc_view_get_type(view);
   return !(type & WLC_BIT_UNMANAGED) && !(type & WLC_BIT_POPUP) && !(type & WLC_BIT_SPLASH);
}

static bool
is_modal(struct wlc_view *view)
{
   uint32_t type = wlc_view_get_type(view);
   return (type & WLC_BIT_MODAL);
}

static bool
is_tiled(struct wlc_view *view)
{
   uint32_t state = wlc_view_get_state(view);
   return !(state & WLC_BIT_FULLSCREEN) && !wlc_view_get_parent(view) && is_managed(view) && !is_or(view) && !is_modal(view);
}

static void
relayout(struct wlc_space *space)
{
   if (!space)
      return;

   struct wl_list *views;
   if (!(views = wlc_space_get_userdata(space)))
      return;

   struct wlc_output *output = wlc_space_get_output(space);
   const struct wlc_size *resolution = wlc_output_get_resolution(output);

   struct wlc_view *v;
   wlc_view_for_each_user(v, views) {
      if (wlc_view_get_state(v) & WLC_BIT_FULLSCREEN)
         wlc_view_set_geometry(v, &(struct wlc_geometry){ { 0, 0 }, *resolution });

      if (wlc_view_get_type(v) & WLC_BIT_SPLASH) {
         struct wlc_geometry g = *wlc_view_get_geometry(v);
         g.origin = (struct wlc_origin){ resolution->w * 0.5 - g.size.w * 0.5, resolution->h * 0.5 - g.size.h * 0.5 };
         wlc_view_set_geometry(v, &g);
      }

      struct wlc_view *parent;
      if (is_managed(v) && !is_or(v) && (parent = wlc_view_get_parent(v)))
         layout_parent(v, parent, &wlc_view_get_geometry(v)->size);
   }

   if (!loliwm.active.layout)
      return;

   loliwm.active.layout->function(space);
}

static void
cycle(struct wlc_compositor *compositor)
{
   struct wl_list *l = wlc_space_get_userdata(wlc_compositor_get_focused_space(compositor));

   if (!l)
      return;

   struct wlc_view *v;
   uint32_t count = 0;
   wlc_view_for_each_user(v, l)
      if (is_tiled(v)) ++count;

   // Check that we have at least two tiled views
   // so we don't get in infinite loop.
   if (count <= 1)
      return;

   // Cycle until we hit next tiled view.
   struct wl_list *p;
   do {
      p = l->prev;
      wl_list_remove(l->prev);
      wl_list_insert(l, p);
   } while (!is_tiled(wlc_view_from_user_link(p)));

   relayout(wlc_compositor_get_focused_space(compositor));
}

static void
raise_all(struct wlc_view *view)
{
   assert(view);

   // Raise view and all related views to top honoring the stacking order.
   struct wlc_view *parent;
   if ((parent = wlc_view_get_parent(view))) {
      raise_all(parent);

      struct wlc_view *v, *vn;
      struct wl_list *views = wlc_space_get_views(wlc_view_get_space(view));
      wlc_view_for_each_safe(v, vn, views) {
         if (v == view || wlc_view_get_parent(v) != parent)
            continue;

         wlc_view_bring_to_front(v);
      }
   }

   wlc_view_bring_to_front(view);
}

static void
set_active(struct wlc_compositor *compositor, struct wlc_view *view)
{
   if (loliwm.active.view == view)
      return;

   // Bemenu should always have focus when open.
   if (loliwm.active.view && (wlc_view_get_state(loliwm.active.view) & BIT_BEMENU)) {
      wlc_view_bring_to_front(loliwm.active.view);
      return;
   }

   if (view) {
      struct wlc_view *v;
      struct wl_list *views = wlc_space_get_views(wlc_view_get_space(view));
      wlc_view_for_each_reverse(v, views) {
         if (wlc_view_get_parent(v) == view) {
            // If window has parent, focus it instead of this.
            // By reverse searching views list, we get the topmost parent.
            set_active(compositor, v);
            return;
         }
      }

      // Only raise fullscreen views when focused view is managed
      if (is_managed(view) && !is_or(view)) {
         wlc_view_for_each_reverse(v, views) {
            if (wlc_view_get_state(v) & WLC_BIT_FULLSCREEN) {
               // Bring the first topmost found fullscreen wlc_view to front.
               // This way we get a "peek" effect when we cycle other views.
               // Meaning the active view is always over fullscreen view,
               // but fullscreen view is on top of the other views.
               wlc_view_bring_to_front(v);
               break;
            }
         }
      }

      // Only set active for current view to false, if new view is on same output and the new view is managed.
      if (loliwm.active.view && is_managed(view) && wlc_space_get_output(wlc_view_get_space(loliwm.active.view)) == wlc_space_get_output(wlc_view_get_space(view)))
         wlc_view_set_state(loliwm.active.view, WLC_BIT_ACTIVATED, false);

      wlc_view_set_state(view, WLC_BIT_ACTIVATED, true);
      raise_all(view);

      wlc_view_for_each_reverse(v, views) {
         if ((wlc_view_get_state(v) & BIT_BEMENU)) {
            // Always bring bemenu to front when exists.
            wlc_view_bring_to_front(v);
            break;
         }
      }
   }

   wlc_compositor_focus_view(compositor, view);
   loliwm.active.view = view;
}

static void
active_space(struct wlc_compositor *compositor, struct wlc_space *space)
{
   struct wl_list *views = wlc_space_get_views(space);

   if (views && !wl_list_empty(views)) {
      set_active(compositor, wlc_view_from_link(views->prev));
   } else {
      set_active(compositor, NULL);
   }
}

static void
focus_next_or_previous_view(struct wlc_compositor *compositor, struct wlc_view *view, bool direction)
{

   struct wl_list *l = wlc_view_get_user_link(view);
   struct wl_list *views = wlc_space_get_userdata(wlc_view_get_space(view));
   if (!l || !views || wl_list_empty(views))
      return;

   int loops = 0;
   do {
      if (!(l = direction ? l ->next : l->prev) || (l == views && (direction ? !(l = l->next) : !(l = l->prev))))
         return;

      struct wlc_view *v;
      if (!(v = wlc_view_from_user_link(l)))
         return;

      set_active(compositor, v);
      if (loliwm.active.view == view)
         loops++;
   } while (loliwm.active.view == view && loops <= 1);
}

static struct wlc_space*
space_for_index(struct wl_list *spaces, int index)
{
   int i = 0;
   struct wlc_space *s;
   wlc_space_for_each(s, spaces) {
      if (index == i)
         return s;
      ++i;
   }
   return NULL;
}

static void
focus_space(struct wlc_compositor *compositor, int index)
{
   struct wlc_output *output;
   if (!(output = wlc_compositor_get_focused_output(compositor)))
      return;

   struct wlc_space *s;
   if ((s = space_for_index(wlc_output_get_spaces(output), index)))
      wlc_output_focus_space(wlc_space_get_output(s), s);
}

static struct wlc_output*
output_for_index(struct wl_list *outputs, int index)
{
   int i = 0;
   struct wlc_output *o;
   wlc_output_for_each(o, outputs) {
      if (index == i)
         return o;
      ++i;
   }
   return NULL;
}

static void
move_to_output(struct wlc_compositor *compositor, struct wlc_view *view, int index)
{
   struct wl_list *outputs = wlc_compositor_get_outputs(compositor);
   struct wlc_output *o = output_for_index(outputs, index);

   if (o) {
      wlc_view_set_space(view, wlc_output_get_active_space(o));
      wlc_compositor_focus_output(compositor, o);
   }
}

static void
move_to_space(struct wlc_compositor *compositor, struct wlc_view *view, int index)
{
   struct wlc_space *active = wlc_compositor_get_focused_space(compositor);
   struct wl_list *spaces = wlc_output_get_spaces(wlc_space_get_output(active));
   struct wlc_space *s = space_for_index(spaces, index);

   if (s)
      wlc_view_set_space(view, s);
}

static void
focus_next_or_previous_output(struct wlc_compositor *compositor, bool direction)
{
   struct wlc_output *active;
   if (!(active = wlc_compositor_get_focused_output(compositor)))
      return;

   struct wl_list *l = (direction ? wlc_output_get_link(active)->next : wlc_output_get_link(active)->prev);
   struct wl_list *outputs = wlc_compositor_get_outputs(compositor);
   if (!l || wl_list_empty(outputs))
      return;

   if (l == outputs && (direction ? !(l = l->next) : !(l = l->prev)))
      return;

   struct wlc_output *o;
   if (!(o = wlc_output_from_link(l)))
      return;

   wlc_compositor_focus_output(compositor, o);
}

static bool
view_created(struct wlc_compositor *compositor, struct wlc_view *view, struct wlc_space *space)
{
   (void)compositor;

   struct wl_list *views;
   if (!(views = wlc_space_get_userdata(space))) {
      if (!(views = calloc(1, sizeof(struct wl_list))))
         return false;

      wl_list_init(views);
      wlc_space_set_userdata(space, views);
   }

   if (wlc_view_get_class(view) && !strcmp(wlc_view_get_class(view), "bemenu")) {
      // Do not allow more than one bemenu instance
      if (loliwm.active.view && wlc_view_get_state(loliwm.active.view) & BIT_BEMENU)
         return false;

      wlc_view_set_state(view, BIT_BEMENU, true); // XXX: Hack
   }

   wl_list_insert(views->prev, wlc_view_get_user_link(view));

   if (should_focus_on_create(view))
      set_active(compositor, view);

   relayout(space);
   wlc_log(WLC_LOG_INFO, "new view: %p (%p)", view, wlc_view_get_parent(view));
   return true;
}

static void
view_destroyed(struct wlc_compositor *compositor, struct wlc_view *view)
{
   wl_list_remove(wlc_view_get_user_link(view));

   if (loliwm.active.view == view) {
      loliwm.active.view = NULL;

      struct wl_list *link = wlc_view_get_link(view);
      struct wlc_view *v = wlc_view_get_parent(view);
      if (v) {
         // Focus the parent view, if there was one
         // Set parent NULL before this to avoid focusing back to dying view
         wlc_view_set_parent(view, NULL);
         set_active(compositor, v);
      } else if (link && link->prev != link->next) {
         // Otherwise focus previous one (stacking order).
         set_active(compositor, wlc_view_from_link(link->prev));
      }
   }

   struct wlc_space *space = wlc_view_get_space(view);
   if (space) {
      relayout(wlc_view_get_space(view));
      struct wl_list *views = wlc_space_get_userdata(space);
      if (views && wl_list_empty(views)) {
         free(views);
         wlc_space_set_userdata(wlc_view_get_space(view), NULL);
      }
   }

   wlc_log(WLC_LOG_INFO, "view destroyed: %p", view);
}

static void
view_switch_space(struct wlc_compositor *compositor, struct wlc_view *view, struct wlc_space *from, struct wlc_space *to)
{
   wl_list_remove(wlc_view_get_user_link(view));
   relayout(from);
   view_created(compositor, view, to);

   if (wlc_space_get_output(from) == wlc_space_get_output(to)) {
      active_space(compositor, from);
   } else {
      struct wlc_view *v;
      wlc_view_for_each_reverse(v, wlc_space_get_views(from)) {
         wlc_view_set_state(v, WLC_BIT_ACTIVATED, true);
         break;
      }
   }

   if (wlc_output_get_active_space(wlc_space_get_output(to)) == to) {
      struct wlc_view *v;
      wlc_view_for_each_reverse(v, wlc_space_get_views(to)) {
         if (v == loliwm.active.view)
            continue;

         wlc_view_set_state(v, WLC_BIT_ACTIVATED, false);
      }
   }
}

static void
view_geometry_request(struct wlc_compositor *compositor, struct wlc_view *view, const struct wlc_geometry *geometry)
{
   (void)compositor;

   uint32_t type = wlc_view_get_type(view);
   uint32_t state = wlc_view_get_state(view);
   bool tiled = is_tiled(view);
   bool action = ((state & WLC_BIT_RESIZING) || (state & WLC_BIT_MOVING));

   if (tiled && !action)
      return;

   if (tiled)
      wlc_view_set_state(view, WLC_BIT_MAXIMIZED, false);

   if ((state & WLC_BIT_FULLSCREEN) || (type & WLC_BIT_SPLASH))
      return;

   struct wlc_view *parent;
   if (is_managed(view) && !is_or(view) && (parent = wlc_view_get_parent(view))) {
      layout_parent(view, parent, &geometry->size);
   } else {
      wlc_view_set_geometry(view, geometry);
   }
}

static void
view_state_request(struct wlc_compositor *compositor, struct wlc_view *view, const enum wlc_view_state_bit state, const bool toggle)
{
   (void)compositor;
   wlc_view_set_state(view, state, toggle);

   wlc_log(WLC_LOG_INFO, "STATE: %d (%d)", state, toggle);
   switch (state) {
      case WLC_BIT_MAXIMIZED:
         if (toggle)
            relayout(wlc_view_get_space(view));
      break;
      case WLC_BIT_FULLSCREEN:
         relayout(wlc_view_get_space(view));
      break;
      default:break;
   }
}

static bool
pointer_button(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state)
{
   (void)time, (void)modifiers, (void)button;

   if (state == WLC_BUTTON_STATE_PRESSED)
      set_active(compositor, view);

   return true;
}

static void
store_rgba(const struct wlc_size *size, uint8_t *rgba)
{
   FILE *f;

   time_t now;
   time(&now);
   char buf[sizeof("loliwm-0000-00-00T00:00:00Z.ppm")];
   strftime(buf, sizeof(buf), "loliwm-%FT%TZ.ppm", gmtime(&now));

   uint8_t *rgb;
   if (!(rgb = calloc(1, size->w * size->h * 3)))
      return;

   if (!(f = fopen(buf, "wb"))) {
      free(rgb);
      return;
   }

   for (uint32_t i = 0, c = 0; i < size->w * size->h * 4; i += 4, c += 3)
      memcpy(rgb + c, rgba + i, 3);

   for (uint32_t i = 0; i * 2 < size->h; ++i) {
      uint32_t o = i * size->w * 3;
      uint32_t r = (size->h - 1 - i) * size->w * 3;
      for (uint32_t i2 = size->w * 3; i2 > 0; --i2, ++o, ++r) {
         uint8_t temp = rgb[o];
         rgb[o] = rgb[r];
         rgb[r] = temp;
      }
   }

   fprintf(f, "P6\n%d %d\n255\n", size->w, size->h);
   fwrite(rgb, 1, size->w * size->h * 3, f);
   free(rgb);
   fclose(f);
}

static void
screenshot(struct wlc_output *output)
{
   if (!output)
      return;

   wlc_output_get_pixels(output, store_rgba);
}

static void
spawn(const char *bin)
{
   if (fork() == 0) {
      setsid();
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      execlp(bin, bin, NULL);
      _exit(EXIT_SUCCESS);
   }
}

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
   if (mods == loliwm.prefix && !syntax_append(prefixed, "P", false))
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
keyboard_key(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state)
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

   const struct keybind *k;
   if (!(k = keybind_for_syntax(prefixed.data)) &&
       !(k = keybind_for_syntax(syntax.data)))
       goto out;

   if (state == WLC_KEY_STATE_PRESSED)
      k->function(compositor, view, time, k->arg);
   pass = false;

out:
   chck_string_release(&syntax);
   chck_string_release(&prefixed);
   return pass;
}

static void
resolution_notify(struct wlc_compositor *compositor, struct wlc_output *output, const struct wlc_size *resolution)
{
   (void)compositor, (void)output, (void)resolution;
   relayout(wlc_output_get_active_space(output));
}

static void
output_notify(struct wlc_compositor *compositor, struct wlc_output *output)
{
   active_space(compositor, wlc_output_get_active_space(output));
}

static void
space_notify(struct wlc_compositor *compositor, struct wlc_space *space)
{
   active_space(compositor, space);
}

static bool
output_created(struct wlc_compositor *compositor, struct wlc_output *output)
{
   (void)compositor;

   // Add some spaces
   for (int i = 1; i < 10; ++i)
      if (!wlc_space_add(output))
         return false;

   return true;
}

static void
die(const char *format, ...)
{
   va_list vargs;
   va_start(vargs, format);
   wlc_vlog(WLC_LOG_ERROR, format, vargs);
   va_end(vargs);
   fflush(stderr);
   exit(EXIT_FAILURE);
}

static uint32_t
parse_prefix(const char *str)
{
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
      if ((prefix & map[i].mod) || strncmp(map[i].name, s, strlen(map[i].name)))
         continue;

      prefix |= map[i].mod;
      s += strlen(map[i].name) + 1;
      if (*(s - 1) != ',')
         break;
      i = 0;
   }

   return (prefix ? prefix : WLC_BIT_MOD_ALT);
}

static void
key_cb_exit(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)compositor, (void)view, (void)time, (void)arg;
   wlc_terminate();
}

static void
key_cb_close_client(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)compositor, (void)time, (void)arg;

   if (!view)
      return;

   wlc_view_close(view);
}

static void
key_cb_spawn(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)compositor, (void)view, (void)time;
   spawn((const char*)arg);
}

static void
key_cb_toggle_fullscreen(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   wlc_view_set_state(view, WLC_BIT_FULLSCREEN, !(wlc_view_get_state(view) & WLC_BIT_FULLSCREEN));
   relayout(wlc_compositor_get_focused_space(compositor));
}

static void
key_cb_cycle_clients(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   cycle(compositor);
}

static void key_cb_focus_space(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time;
   focus_space(compositor, (uint32_t)arg);
}

static void key_cb_move_to_output(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)time;

   if (!view)
      return;

   move_to_output(compositor, view, (uint32_t)arg);
}

static void key_cb_move_to_space(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)time;

   if (!view)
      return;

   move_to_space(compositor, view, (uint32_t)arg);
}

static void
key_cb_focus_next_output(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_output(compositor, true);
}

static void
key_cb_focus_previous_client(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   focus_next_or_previous_view(compositor, view, false);
}

static void
key_cb_focus_next_client(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   focus_next_or_previous_view(compositor, view, true);
}

static void
key_cb_take_screenshot(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   screenshot(wlc_compositor_get_focused_output(compositor));
}

static bool
setup_default_keybinds(void)
{
   const char *terminal = getenv("TERMINAL");
   chck_string_set_cstr(&loliwm.terminal, (terminal && strlen(terminal) ? terminal : DEFAULT_TERMINAL), true);

   return (add_keybind("exit", "<P-Escape>", key_cb_exit, 0) &&
           add_keybind("close client", "<P-q>", key_cb_close_client, 0) &&
           add_keybind("spawn terminal", "<P-Return>", key_cb_spawn, (intptr_t)loliwm.terminal.data) &&
           add_keybind("spawn bemenu", "<P-p>", key_cb_spawn, (intptr_t)DEFAULT_MENU) &&
           add_keybind("toggle fullscreen", "<P-f>", key_cb_toggle_fullscreen, 0) &&
           add_keybind("cycle clients", "<P-h>", key_cb_cycle_clients, 0) &&
           add_keybind("focus next output", "<P-l>", key_cb_focus_next_output, 0) &&
           add_keybind("focus next client", "<P-k>", key_cb_focus_next_client, 0) &&
           add_keybind("focus previous client", "<P-j>", key_cb_focus_previous_client, 0) &&
           add_keybind("take screenshot", "<P-SunPrint_Screen>", key_cb_take_screenshot, 0) &&
           add_keybind("focus space 0", "<P-0>", key_cb_focus_space, 0) &&
           add_keybind("focus space 1", "<P-1>", key_cb_focus_space, 1) &&
           add_keybind("focus space 2", "<P-2>", key_cb_focus_space, 2) &&
           add_keybind("focus space 3", "<P-3>", key_cb_focus_space, 3) &&
           add_keybind("focus space 4", "<P-4>", key_cb_focus_space, 4) &&
           add_keybind("focus space 5", "<P-5>", key_cb_focus_space, 5) &&
           add_keybind("focus space 6", "<P-6>", key_cb_focus_space, 6) &&
           add_keybind("focus space 7", "<P-7>", key_cb_focus_space, 7) &&
           add_keybind("focus space 8", "<P-8>", key_cb_focus_space, 8) &&
           add_keybind("focus space 9", "<P-9>", key_cb_focus_space, 9) &&
           add_keybind("move to space 0", "<P-F0>", key_cb_move_to_space, 0) &&
           add_keybind("move to space 1", "<P-F1>", key_cb_move_to_space, 1) &&
           add_keybind("move to space 2", "<P-F2>", key_cb_move_to_space, 2) &&
           add_keybind("move to space 3", "<P-F3>", key_cb_move_to_space, 3) &&
           add_keybind("move to space 4", "<P-F4>", key_cb_move_to_space, 4) &&
           add_keybind("move to space 5", "<P-F5>", key_cb_move_to_space, 5) &&
           add_keybind("move to space 6", "<P-F6>", key_cb_move_to_space, 6) &&
           add_keybind("move to space 7", "<P-F7>", key_cb_move_to_space, 7) &&
           add_keybind("move to space 8", "<P-F8>", key_cb_move_to_space, 8) &&
           add_keybind("move to space 9", "<P-F9>", key_cb_move_to_space, 9) &&
           add_keybind("move to output 0", "<P-z>", key_cb_move_to_output, 0) &&
           add_keybind("move to output 1", "<P-x>", key_cb_move_to_output, 1) &&
           add_keybind("move to output 2", "<P-c>", key_cb_move_to_output, 2));
}

static bool
plugins_init(void)
{
   {
      static const struct method methods[] = {
         REGISTER_METHOD(is_tiled, "b(p)|1"),
         REGISTER_METHOD(relayout, "v(p)|1"),
         REGISTER_METHOD(add_layout, "b(c[],p)|1"),
         REGISTER_METHOD(remove_layout, "v(c[])|1"),
         REGISTER_METHOD(add_keybind, "b(c[],c[],p,ip)|1"),
         REGISTER_METHOD(remove_keybind, "v(c[])|1"),
         {0},
      };

      struct plugin core = {
         .info = {
            .name = "loliwm",
            .version = "1.0.0",
            .methods = methods,
      }, {0}};

      if (!register_plugin(&core, NULL))
         return false;
   }

   {
      register_plugin_from_path("plugins/loliwm-plugin-test.so");
   }

   {
      register_plugin_from_path("plugins/loliwm-plugin-core-layouts.so");
   }

   return true;
}

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   if (!chck_iter_pool(&loliwm.layouts.pool, 32, 0, sizeof(struct layout)) ||
       !chck_pool(&loliwm.keybinds.pool, 32, 0, sizeof(struct keybind)) ||
       !chck_hash_table(&loliwm.keybinds.table, -1, 256, sizeof(size_t)))
      return EXIT_FAILURE;

   static const struct wlc_interface interface = {
      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .switch_space = view_switch_space,

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

      .output = {
         .created = output_created,
         .activated = output_notify,
         .resolution = resolution_notify,
      },

      .space = {
         .activated = space_notify,
      },
   };

   if (!wlc_init(&interface, argc, argv))
      return EXIT_FAILURE;

   struct wlc_compositor *compositor;
   if (!(compositor = wlc_compositor_new(&loliwm)))
      return EXIT_FAILURE;

   struct sigaction action = {
      .sa_handler = SIG_DFL,
      .sa_flags = SA_NOCLDWAIT
   };

   // do not care about childs
   sigaction(SIGCHLD, &action, NULL);

   for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "--prefix")) {
         if (i + 1 >= argc)
            die("--prefix takes an argument (shift,caps,ctrl,alt,logo,mod2,mod3,mod5)");
         loliwm.prefix = parse_prefix(argv[++i]);
      }
   }

   if (!setup_default_keybinds())
      return EXIT_FAILURE;

   if (!plugins_init())
      return EXIT_FAILURE;

   wlc_log(WLC_LOG_INFO, "loliwm started");
   wlc_run();

   memset(&loliwm, 0, sizeof(loliwm));
   wlc_log(WLC_LOG_INFO, "-!- loliwm is gone, bye bye!");
   return EXIT_SUCCESS;
}
