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

// XXX: hack
enum {
   BIT_BEMENU = 1<<5,
};

static struct {
   struct wlc_compositor *compositor;
   struct wlc_view *active;
   float cut;
   uint32_t prefix;
} loliwm = {
   .cut = 0.5f,
   .prefix = WLC_BIT_MOD_ALT,
};

static void
layout_parent(struct wlc_view *view, struct wlc_view *parent, uint32_t w, uint32_t h)
{
   assert(view && parent);

   // Size to fit the undermost parent
   // TODO: Use surface height as base instead of current
   struct wlc_view *under;
   for (under = parent; under && wlc_view_get_parent(under); under = wlc_view_get_parent(under));
   uint32_t uw = wlc_view_get_width(under);
   uint32_t uh = wlc_view_get_height(under);
   uint32_t tw = (w > uw * 0.8 ? uw * 0.8 : w);
   uint32_t th = (h > uh * 0.8 ? uh * 0.8 : h);

   // Center the parent
   uint32_t pw = wlc_view_get_width(parent);
   uint32_t ph = wlc_view_get_height(parent);
   wlc_view_position(view, pw * 0.5 - tw * 0.5, ph * 0.5 - th * 0.5);
   wlc_view_resize(view, tw, th);
}

static bool
should_focus_on_create(struct wlc_view *view)
{
   // Do not allow unmanaged views to steal focus (tooltips, dnds, etc..)
   // Do not allow parented windows to steal focus, if current window wasn't parent.
   uint32_t type = wlc_view_get_type(view);
   struct wlc_view *parent = wlc_view_get_parent(view);
   return (!(type & WLC_BIT_UNMANAGED) && (!loliwm.active || !parent || parent == loliwm.active));
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

   uint32_t rwidth, rheight;
   struct wlc_output *output = wlc_space_get_output(space);
   wlc_output_get_resolution(output, &rwidth, &rheight);

   struct wlc_view *v;
   uint32_t count = 0;
   wlc_view_for_each_user(v, views)
      if (is_tiled(v)) ++count;

   bool toggle = false;
   uint32_t y = 0, height = rheight / (count > 1 ? count - 1 : 1);
   uint32_t fheight = (rheight > height * (count - 1) ? height + (rheight - height * (count - 1)) : height);
   wlc_view_for_each_user(v, views) {
      if (wlc_view_get_state(v) & WLC_BIT_FULLSCREEN) {
         wlc_view_resize(v, rwidth, rheight);
         wlc_view_position(v, 0, 0);
      }

      if (wlc_view_get_type(v) & WLC_BIT_SPLASH)
         wlc_view_position(v, rwidth * 0.5 - wlc_view_get_width(v) * 0.5, rheight * 0.5 - wlc_view_get_height(v) * 0.5);

      struct wlc_view *parent;
      if (is_managed(v) && !is_or(v) && (parent = wlc_view_get_parent(v)))
         layout_parent(v, parent, wlc_view_get_width(v), wlc_view_get_height(v));

      if (!is_tiled(v))
         continue;

      uint32_t slave = rwidth * loliwm.cut;
      wlc_view_set_state(v, WLC_BIT_MAXIMIZED, true);
      wlc_view_resize(v, (count > 1 ? (toggle ? slave : rwidth - slave) : rwidth), (toggle ? (y == 0 ? fheight : height) : rheight));
      wlc_view_position(v, (toggle ? rwidth - slave : 0), y);

      if (toggle)
         y += (y == 0 ? fheight : height);

      toggle = true;
   }
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
   if (loliwm.active == view)
      return;

   // Bemenu should always have focus when open.
   if (loliwm.active && (wlc_view_get_state(loliwm.active) & BIT_BEMENU)) {
      wlc_view_bring_to_front(loliwm.active);
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
      if (loliwm.active && is_managed(view) && wlc_space_get_output(wlc_view_get_space(loliwm.active)) == wlc_space_get_output(wlc_view_get_space(view)))
         wlc_view_set_state(loliwm.active, WLC_BIT_ACTIVATED, false);

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
   loliwm.active = view;
}

static void
focus_next_or_previous_view(struct wlc_compositor *compositor, struct wlc_view *view, bool direction)
{

   struct wl_list *l = (direction ? wlc_view_get_user_link(view)->next : wlc_view_get_user_link(view)->prev);
   struct wl_list *views = wlc_space_get_userdata(wlc_view_get_space(view));
   if (!l || wl_list_empty(views))
      return;

   if (l == views && (direction ? !(l = l->next) : !(l = l->prev)))
      return;

   struct wlc_view *v;
   if (!(v = wlc_view_from_user_link(l)))
      return;

   set_active(compositor, v);
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
   struct wlc_space *active = wlc_compositor_get_focused_space(compositor);
   struct wl_list *spaces = wlc_output_get_spaces(wlc_space_get_output(active));
   struct wlc_space *s = space_for_index(spaces, index);

   if (s)
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
      if (loliwm.active && wlc_view_get_state(loliwm.active) & BIT_BEMENU)
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

   if (loliwm.active == view) {
      loliwm.active = NULL;

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

   relayout(wlc_view_get_space(view));

   struct wl_list *views = wlc_space_get_userdata(wlc_view_get_space(view));
   if (wl_list_empty(views)) {
      free(views);
      wlc_space_set_userdata(wlc_view_get_space(view), NULL);
   }

   wlc_log(WLC_LOG_INFO, "view destroyed: %p", view);
}

static void
view_switch_space(struct wlc_compositor *compositor, struct wlc_view *view, struct wlc_space *from, struct wlc_space *to)
{
   wl_list_remove(wlc_view_get_user_link(view));
   relayout(from);
   view_created(compositor, view, to);

   if (wlc_space_get_output(from) == wlc_space_get_output(to))
      wlc_output_focus_space(wlc_space_get_output(from), from);

   if (wlc_output_get_active_space(wlc_space_get_output(to)) == to) {
      struct wlc_view *v;
      wlc_view_for_each_reverse(v, wlc_space_get_views(to)) {
         if (v == loliwm.active)
            continue;

         wlc_view_set_state(v, WLC_BIT_ACTIVATED, false);
      }
   }
}

static void
view_geometry_request(struct wlc_compositor *compositor, struct wlc_view *view, int32_t x, int32_t y, uint32_t w, uint32_t h)
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
      layout_parent(view, parent, w, h);
   } else {
      wlc_view_position(view, x, y);
      wlc_view_resize(view, w, h);
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
pointer_button(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t button, enum wlc_button_state state)
{
   (void)button;

   if (state == WLC_BUTTON_STATE_PRESSED)
      set_active(compositor, view);

   return true;
}

static void
screenshot(struct wlc_output *output)
{
   if (!output)
      return;

   time_t now;
   time(&now);
   char buf[sizeof("loliwm-0000-00-00T00:00:00Z.ppm")];
   strftime(buf, sizeof(buf), "loliwm-%FT%TZ.ppm", gmtime(&now));

   uint32_t w, h;
   wlc_output_get_resolution(output, &w, &h);

   unsigned char *pixels = NULL;
   if (!(pixels = calloc(1, w * h * 3 + w * h * 4)))
      goto fail;

   unsigned char *rgb = pixels, *rgba = pixels + w * h * 3;
   if (!wlc_output_get_pixels(output, rgba))
      goto fail;

   FILE *f;
   if (!(f = fopen(buf, "wb")))
      goto fail;

   for (uint32_t i = 0, c = 0; i < w * h * 4; i += 4, c += 3)
      memcpy(rgb + c, rgba + i, 3);

   for (uint32_t i = 0; i * 2 < h; ++i) {
      uint32_t o = i * w * 3;
      uint32_t r = (h - 1 - i) * w * 3;
      for (uint32_t i2 = w * 3; i2 > 0; --i2, ++o, ++r) {
         uint8_t temp = rgb[o];
         rgb[o] = rgb[r];
         rgb[r] = temp;
      }
   }

   fprintf(f, "P6\n%d %d\n255\n", w, h);
   fwrite(pixels, 1, w * h * 3, f);
   free(pixels);
   fclose(f);
   return;

fail:
   free(pixels);
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
keyboard_key(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t leds, uint32_t mods, uint32_t key, enum wlc_key_state state)
{
   (void)leds;

   bool pass = true;
   if (mods == loliwm.prefix) {
      if (key == 1) {
         if (state == WLC_KEY_STATE_PRESSED)
            wlc_compositor_terminate(compositor);
         pass = false;
      } else if (view && key == 16) {
         if (state == WLC_KEY_STATE_PRESSED)
            wlc_view_close(view);
         pass = false;
      } else if (key == 28) {
         if (state == WLC_KEY_STATE_PRESSED) {
            const char *terminal = getenv("TERMINAL");
            terminal = (terminal ? terminal : "weston-terminal");
            spawn(terminal);
         }
         pass = false;
      } else if (key == 25) {
         if (state == WLC_KEY_STATE_PRESSED)
            spawn("bemenu-run");
         pass = false;
      } else if (view && key == 33) {
         if (state == WLC_KEY_STATE_PRESSED) {
            wlc_view_set_state(view, WLC_BIT_FULLSCREEN, !(wlc_view_get_state(view) & WLC_BIT_FULLSCREEN));
            relayout(wlc_compositor_get_focused_space(compositor));
         }
         pass = false;
      } else if (key == 35) {
         if (state == WLC_KEY_STATE_PRESSED)
            cycle(compositor);
         pass = false;
      } else if (key >= 2 && key <= 11) {
         if (state == WLC_KEY_STATE_PRESSED)
            focus_space(compositor, key - 2);
         pass = false;
      } else if (key >= 23 && key <= 24) {
         if (state == WLC_KEY_STATE_PRESSED) {
            loliwm.cut += (key == 23 ? -0.01 : 0.01);
            if (loliwm.cut > 1.0) loliwm.cut = 1.0;
            if (loliwm.cut < 0.0) loliwm.cut = 0.0;
            relayout(wlc_compositor_get_focused_space(compositor));
         }
         pass = false;
      } else if (view && key >= 44 && key <= 46) {
         if (state == WLC_KEY_STATE_PRESSED)
            move_to_output(compositor, view, key - 44);
         pass = false;
      } else if (view && key >= 59 && key <= 68) {
         if (state == WLC_KEY_STATE_PRESSED)
            move_to_space(compositor, view, key - 59);
         pass = false;
      } else if (key == 38) {
         if (state == WLC_KEY_STATE_PRESSED)
            focus_next_or_previous_output(compositor, true);
         pass = false;
      } else if (view && key == 37) {
         if (state == WLC_KEY_STATE_PRESSED)
            focus_next_or_previous_view(compositor, view, false);
         pass = false;
      } else if (view && key == 36) {
         if (state == WLC_KEY_STATE_PRESSED)
            focus_next_or_previous_view(compositor, view, true);
         pass = false;
      } else if (key == 99) {
         if (state == WLC_KEY_STATE_PRESSED)
            screenshot(wlc_compositor_get_focused_output(compositor));
         pass = false;
      }
   }

   if (pass)
      printf("(%p) KEY: %u\n", view, key);

   return pass;
}

static void
resolution_notify(struct wlc_compositor *compositor, struct wlc_output *output, uint32_t width, uint32_t height)
{
   (void)compositor, (void)output, (void)width, (void)height;
   relayout(wlc_output_get_active_space(output));
}

static void
output_notify(struct wlc_compositor *compositor, struct wlc_output *output)
{
   struct wl_list *views = wlc_space_get_views(wlc_output_get_active_space(output));

   if (!wl_list_empty(views)) {
      set_active(compositor, wlc_view_from_link(views->prev));
   } else {
      set_active(compositor, NULL);
   }
}

static void
space_notify(struct wlc_compositor *compositor, struct wlc_space *space)
{
   struct wl_list *views = wlc_space_get_views(space);

   if (!wl_list_empty(views)) {
      set_active(compositor, wlc_view_from_link(views->prev));
   } else {
      set_active(compositor, NULL);
   }
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
terminate(void)
{
   if (loliwm.compositor)
      wlc_compositor_free(loliwm.compositor);

   memset(&loliwm, 0, sizeof(loliwm));
   wlc_log(WLC_LOG_INFO, "-!- loliwm is gone, bye bye!");
}

static bool
initialize(void)
{
   struct wlc_interface interface = {
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

   if (!(loliwm.compositor = wlc_compositor_new(&interface)))
      goto fail;

   return true;

fail:
   terminate();
   return false;
}

static void
run(void)
{
   wlc_log(WLC_LOG_INFO, "loliwm started");
   wlc_compositor_run(loliwm.compositor);
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

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   if (!wlc_init(argc, argv))
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

   if (!initialize())
      return EXIT_FAILURE;

   run();
   terminate();
   return EXIT_SUCCESS;
}
