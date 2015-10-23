#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <orbment/plugin.h>
#include <chck/math/math.h>
#include <chck/string/string.h>
#include "common.h"
#include "config.h"

#define DEFAULT_TERMINAL "weston-terminal"
#define DEFAULT_MENU "bemenu-run"

static void (*relayout)(wlc_handle output);

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(plugin_h, const char *name, const char **syntax, const struct function*, intptr_t arg);
static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   struct {
      wlc_handle view;
      struct wlc_origin grab;
      uint32_t edges;
   } action;

   struct {
      wlc_handle view;
   } active;

   struct {
      bool follow_focus;
   } config;

   struct chck_string terminal;
   plugin_h self;
} plugin;

static bool
start_interactive_action(wlc_handle view, const struct wlc_origin *origin)
{
   if (plugin.action.view)
      return false;

   plugin.action.view = view;
   plugin.action.grab = *origin;
   wlc_view_bring_to_front(view);
   return true;
}

static void
start_interactive_move(wlc_handle view, const struct wlc_origin *origin)
{
   start_interactive_action(view, origin);
}

static void
start_interactive_resize(wlc_handle view, uint32_t edges, const struct wlc_origin *origin)
{
   const struct wlc_geometry *g;
   if (!(g = wlc_view_get_geometry(view)) || !start_interactive_action(view, origin))
      return;

   const int32_t halfw = g->origin.x + g->size.w / 2;
   const int32_t halfh = g->origin.y + g->size.h / 2;

   if (!(plugin.action.edges = edges)) {
      plugin.action.edges = (origin->x < halfw ? WLC_RESIZE_EDGE_LEFT : (origin->x > halfw ? WLC_RESIZE_EDGE_RIGHT : 0)) |
                            (origin->y < halfh ? WLC_RESIZE_EDGE_TOP : (origin->y > halfh ? WLC_RESIZE_EDGE_BOTTOM : 0));
   }

   wlc_view_set_state(view, WLC_BIT_RESIZING, true);
}

static void
stop_interactive_action(void)
{
   if (!plugin.action.view)
      return;

   wlc_view_set_state(plugin.action.view, WLC_BIT_RESIZING, false);
   memset(&plugin.action, 0, sizeof(plugin.action));
}

static wlc_handle
get_next_view(wlc_handle view, size_t offset, enum direction dir)
{
   size_t memb, i;
   wlc_handle *views = wlc_output_get_mutable_views((view ? wlc_view_get_output(view) : wlc_get_focused_output()), &memb);
   for (i = 0; i < memb && views[i] != view; ++i);
   return (memb > 0 ? views[(dir == PREV ? chck_clampsz(i - offset, 0, memb - 1) : i + offset) % memb] : 0);
}

PPURE static uint32_t
rotate_mask(uint32_t mask, size_t offset, enum direction dir)
{
   // Shifting with the total bits or more of mask variable causes UB
   assert(sizeof(mask) * CHAR_BIT >= offset);

   // XXX: Hardcoded to rotate range of 10 spaces.
   //      Not yet sure how Orbment space handling eventually works.
   //      But right now this is convenient due to Orbment giving you keybindings to 10 spaces.
   const uint32_t range = 10;

   // Circular shift from https://en.wikipedia.org/wiki/Circular_shift
   // Modified to shift only a subsection of the bits

   // Bitmask for the spaces in range, toggled bits define the spaces to shift, untoggled bits are left alone
   const uint32_t whitemask = (uint32_t)((uint64_t)1 << range) - 1;

   // Take the spaces in range and bitrotate them while ignoring spaces outside of the range
   const uint32_t spaces = mask & whitemask;

   uint32_t newmask;
   if (PREV == dir)
      newmask = (spaces >> offset) | (spaces << (range - offset));
   else
      newmask = (spaces << offset) | (spaces >> (range - offset));

   // Truncate bitmask to the range and append the bits outside of the range.
   return (newmask & whitemask) | (~whitemask & mask);
}

static wlc_handle
get_next_output(wlc_handle output, size_t offset, enum direction dir)
{
   size_t memb, i;
   const wlc_handle *outputs = wlc_get_outputs(&memb);
   for (i = 0; i < memb && outputs[i] != output; ++i);
   return (memb > 0 ? outputs[(dir == PREV ? chck_clampsz(i - offset, 0, memb - 1) : i + offset) % memb] : 0);
}

static bool
should_focus_on_create(wlc_handle view)
{
   // Do not allow unmanaged views to steal focus (tooltips, dnds, etc..)
   // Do not allow parented windows to steal focus, if current window wasn't parent.
   const wlc_handle parent = wlc_view_get_parent(view);
   return (is_managed(view) && (!plugin.active.view || !parent || parent == plugin.active.view));
}

static void
raise_all(wlc_handle view)
{
   assert(view);

   if (!is_managed(view))
      return;

   // Raise view and all related views to top honoring the stacking order.
   wlc_handle parent;
   if ((parent = wlc_view_get_parent(view))) {
      raise_all(parent);

      size_t memb;
      const wlc_handle *views = wlc_output_get_views(wlc_view_get_output(view), &memb);
      for (size_t i = 0; i < memb; ++i) {
         if (views[i] == view || wlc_view_get_parent(views[i]) != parent)
            continue;

         wlc_view_bring_to_front(views[i]);
      }
   }

   wlc_view_bring_to_front(view);
}

static void
focus_view(wlc_handle view)
{
   if (plugin.active.view == view)
      return;

   if (!is_managed(view))
      return;

   // Bemenu should always have focus when open.
   if (plugin.active.view && (wlc_view_get_type(plugin.active.view) & BIT_BEMENU)) {
      wlc_view_bring_to_front(plugin.active.view);
      return;
   }

   if (view) {
      {
         size_t memb;
         const wlc_handle *views = wlc_output_get_views(wlc_view_get_output(view), &memb);
         for (size_t i = memb; i > 0; --i) {
            if (wlc_view_get_parent(views[i - 1]) == view) {
               // If window has parent, focus it instead of this.
               // By reverse searching views list, we get the topmost parent.
               focus_view(views[i - 1]);
               return;
            }
         }
      }

      // Only raise fullscreen views when focused view is managed
      if (is_managed(view) && !is_or(view)) {
         size_t memb;
         const wlc_handle *views = wlc_output_get_views(wlc_view_get_output(view), &memb);
         for (size_t i = memb; i > 0; --i) {
            if (wlc_view_get_state(views[i - 1]) & WLC_BIT_FULLSCREEN) {
               // Bring the first topmost found fullscreen wlc_view to front.
               // This way we get a "peek" effect when we cycle other views.
               // Meaning the active view is always over fullscreen view,
               // but fullscreen view is on top of the other views.
               wlc_view_bring_to_front(views[i - 1]);
               break;
            }
         }
      }

      raise_all(view);

      {
         size_t memb;
         const wlc_handle *views = wlc_output_get_views(wlc_view_get_output(view), &memb);
         for (size_t i = memb; i > 0; --i) {
            if ((wlc_view_get_type(views[i - 1]) & BIT_BEMENU)) {
               // Always bring bemenu to front when exists.
               wlc_view_bring_to_front(views[i - 1]);
               break;
            }
         }
      }
   }

   wlc_view_focus(view);
   plugin.active.view = view;
}

static void
focus_next_or_previous_view(wlc_handle view, enum direction direction)
{
   wlc_handle first = get_next_view(view, 0, direction), v = first, old = plugin.active.view;

   if (!first)
      return;

   do {
      while ((v = get_next_view(v, 1, direction)) && v != first && wlc_view_get_mask(v) != wlc_output_get_mask(wlc_view_get_output(view)));
      if (wlc_view_get_mask(v) == wlc_output_get_mask(wlc_get_focused_output()))
         focus_view(v);
   } while (plugin.active.view && plugin.active.view == old && v != old);
}

static void
focus_topmost(wlc_handle output)
{
   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);
   for (size_t i = memb; i > 0; --i) {
      if (wlc_view_get_mask(views[i - 1]) != wlc_output_get_mask(output))
         continue;

      focus_view(views[i - 1]);
      return;
   }

   // There is no topmost
   focus_view(0);
}

static void
focus_space(uint32_t index)
{
   wlc_output_set_mask(wlc_get_focused_output(), (1<<index));
   focus_topmost(wlc_get_focused_output());
   relayout(wlc_get_focused_output());
}

static void
move_to_space(wlc_handle view, uint32_t index)
{
   wlc_view_set_mask(view, (1<<index));
   focus_space(index);
}

static void
focus_next_or_previous_space(enum direction direction)
{
   const wlc_handle output = wlc_get_focused_output();
   wlc_output_set_mask(output, rotate_mask(wlc_output_get_mask(output), 1, direction));
   focus_topmost(output);
   relayout(output);
}

static wlc_handle
output_for_index(uint32_t index)
{
   size_t memb;
   const wlc_handle *outputs = wlc_get_outputs(&memb);
   return (index < memb ? outputs[index] : 0);
}

static void
focus_output(wlc_handle output)
{
   wlc_output_focus(output);
   focus_topmost(wlc_get_focused_output());
   relayout(output);
}

static void
move_to_output(wlc_handle view, uint32_t index)
{
   wlc_handle output;
   if (!(output = output_for_index(index)))
      return;

   wlc_view_set_mask(view, wlc_output_get_mask(output));
   wlc_view_set_output(view, output);
   focus_output(output);
}

static void
focus_next_or_previous_output(enum direction direction)
{
   focus_output(get_next_output(wlc_get_focused_output(), 1, direction));
}

static void
set_active_view_on_output(wlc_handle output, wlc_handle view)
{
   size_t memb;
   const wlc_handle *views = wlc_output_get_views(output, &memb);
   for (size_t i = 0; i < memb; ++i)
      wlc_view_set_state(views[i], WLC_BIT_ACTIVATED, (views[i] == view));
}

static void
view_move_to_output(wlc_handle view, wlc_handle from, wlc_handle to)
{
   (void)view;

   relayout(from);
   relayout(to);
   plog(plugin.self, PLOG_INFO, "view %zu moved from output %zu to %zu", view, from, to);

   if (wlc_view_get_state(view) & WLC_BIT_ACTIVATED)
      set_active_view_on_output(to, view);

   focus_topmost(from);
}

static void
view_move_request(wlc_handle view, const struct wlc_origin *origin)
{
   start_interactive_move(view, origin);
}

static void
view_resize_request(wlc_handle view, uint32_t edges, const struct wlc_origin *origin)
{
   start_interactive_resize(view, edges, origin);
}

static void
view_focus(wlc_handle view, bool focus)
{
   if (wlc_view_get_output(view) == wlc_get_focused_output())
      wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
}

static bool
view_created(wlc_handle view)
{
   if (wlc_view_get_class(view) && chck_cstreq(wlc_view_get_class(view), "bemenu")) {
      // Do not allow more than one bemenu instance
      if (plugin.active.view && wlc_view_get_type(plugin.active.view) & BIT_BEMENU)
         return false;

      wlc_view_set_type(view, BIT_BEMENU, true); // XXX: Hack
   }

   if (should_focus_on_create(view)) {
      if (wlc_view_get_output(view) == wlc_get_focused_output()) {
         focus_view(view);
      } else {
         set_active_view_on_output(wlc_view_get_output(view), view);
      }
   }

   relayout(wlc_view_get_output(view));
   return true;
}

static void
view_destroyed(wlc_handle view)
{
   if (plugin.active.view == view) {
      plugin.active.view = 0;

      wlc_handle v;
      if ((v = wlc_view_get_parent(view))) {
         // Focus the parent view, if there was one
         // Set parent 0 before this to avoid focusing back to dying view
         wlc_view_set_parent(view, 0);
         focus_view(v);
      } else {
         // Otherwise focus previous one.
         focus_topmost(wlc_view_get_output(view));
      }
   }

   relayout(wlc_view_get_output(view));
}

static bool
pointer_motion(wlc_handle view, uint32_t time, const struct wlc_origin *motion)
{
   (void)time;

   wlc_pointer_set_origin(motion);

   if (plugin.action.view) {
      const int32_t dx = motion->x - plugin.action.grab.x;
      const int32_t dy = motion->y - plugin.action.grab.y;
      struct wlc_geometry g = *wlc_view_get_geometry(plugin.action.view);

      if (plugin.action.edges) {
         const struct wlc_size min = { 80, 40 };

         struct wlc_geometry n = g;
         if (plugin.action.edges & WLC_RESIZE_EDGE_LEFT) {
            n.size.w -= dx;
            n.origin.x += dx;
         } else if (plugin.action.edges & WLC_RESIZE_EDGE_RIGHT) {
            n.size.w += dx;
         }

         if (plugin.action.edges & WLC_RESIZE_EDGE_TOP) {
            n.size.h -= dy;
            n.origin.y += dy;
         } else if (plugin.action.edges & WLC_RESIZE_EDGE_BOTTOM) {
            n.size.h += dy;
         }

         if (n.size.w >= min.w) {
            g.origin.x = n.origin.x;
            g.size.w = n.size.w;
         }

         if (n.size.h >= min.h) {
            g.origin.y = n.origin.y;
            g.size.h = n.size.h;
         }

         wlc_view_set_geometry(plugin.action.view, plugin.action.edges, &g);
      } else {
         g.origin.x += dx;
         g.origin.y += dy;
         wlc_view_set_geometry(plugin.action.view, 0, &g);
      }

      plugin.action.grab = *motion;
   } else if (plugin.config.follow_focus) {
      focus_view(view);
   }

   return (plugin.action.view ? true : false);
}

static bool
pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers, uint32_t button, enum wlc_button_state state, const struct wlc_origin *origin)
{
   (void)view, (void)time, (void)modifiers, (void)button, (void)origin;

   if (state == WLC_BUTTON_STATE_RELEASED)
      stop_interactive_action();

   return (plugin.action.view ? true : false);
}

static void
key_cb_exit(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   wlc_terminate();
}

static void
key_cb_close_client(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   wlc_view_close(view);
}

static void
key_cb_spawn_terminal(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   wlc_exec(plugin.terminal.data, (char *const[]){ plugin.terminal.data, NULL });
}

static void
key_cb_spawn_bemenu(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   wlc_exec(DEFAULT_MENU, (char *const[]){ DEFAULT_MENU, NULL });
}

static void
key_cb_toggle_fullscreen(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   wlc_view_set_state(view, WLC_BIT_FULLSCREEN, !(wlc_view_get_state(view) & WLC_BIT_FULLSCREEN));
   relayout(wlc_view_get_output(view));
}

static void
key_cb_focus_space(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time;
   focus_space((uint32_t)arg);
}

static void
key_cb_focus_previous_space(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_space(PREV);
}

static void
key_cb_focus_next_space(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_space(NEXT);
}

static void
key_cb_move_to_output(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time;

   if (!view)
      return;

   move_to_output(view, (uint32_t)arg);
}

static void
key_cb_move_to_space(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time;

   if (!view)
      return;

   move_to_space(view, (uint32_t)arg);
}

static void
key_cb_focus_next_output(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_output(NEXT);
}

static void
key_cb_focus_previous_client(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_view(plugin.active.view, PREV);
}

static void
key_cb_focus_next_client(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time, (void)arg;
   focus_next_or_previous_view(plugin.active.view, NEXT);
}

static void
key_cb_focus_view(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   focus_view(view);
}

static void
key_cb_move_view(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   struct wlc_origin o;
   wlc_pointer_get_origin(&o);
   start_interactive_move(view, &o);
}

static void
key_cb_resize_view(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)time, (void)arg;

   if (!view)
      return;

   struct wlc_origin o;
   wlc_pointer_get_origin(&o);
   start_interactive_resize(view, 0, &o);
}

static const struct {
   const char *name, **syntax;
   keybind_fun_t function;
   intptr_t arg;
} keybinds[] = {
   { "exit", (const char*[]){ "<P-Escape>", NULL }, key_cb_exit, 0 },
   { "close client", (const char*[]){ "<P-q>", NULL }, key_cb_close_client, 0 },
   { "spawn terminal", (const char*[]){ "<P-Return>", NULL }, key_cb_spawn_terminal, 0 },
   { "spawn bemenu", (const char*[]){ "<P-p>", NULL }, key_cb_spawn_bemenu, 0 },
   { "toggle fullscreen", (const char*[]){ "<P-f>", NULL }, key_cb_toggle_fullscreen, 0 },
   { "focus next output", (const char*[]){ "<P-l>", NULL }, key_cb_focus_next_output, 0 },
   { "focus next client", (const char*[]){ "<P-j>", NULL }, key_cb_focus_next_client, 0 },
   { "focus previous client", (const char*[]){ "<P-k>", NULL }, key_cb_focus_previous_client, 0 },
   { "focus space 0", (const char*[]){ "<P-1>", "<P-KP_1>", NULL }, key_cb_focus_space, 0 },
   { "focus space 1", (const char*[]){ "<P-2>", "<P-KP_2>", NULL }, key_cb_focus_space, 1 },
   { "focus space 2", (const char*[]){ "<P-3>", "<P-KP_3>", NULL }, key_cb_focus_space, 2 },
   { "focus space 3", (const char*[]){ "<P-4>", "<P-KP_4>", NULL }, key_cb_focus_space, 3 },
   { "focus space 4", (const char*[]){ "<P-5>", "<P-KP_5>", NULL }, key_cb_focus_space, 4 },
   { "focus space 5", (const char*[]){ "<P-6>", "<P-KP_6>", NULL }, key_cb_focus_space, 5 },
   { "focus space 6", (const char*[]){ "<P-7>", "<P-KP_7>", NULL }, key_cb_focus_space, 6 },
   { "focus space 7", (const char*[]){ "<P-8>", "<P-KP_8>", NULL }, key_cb_focus_space, 7 },
   { "focus space 8", (const char*[]){ "<P-9>", "<P-KP_9>", NULL }, key_cb_focus_space, 8 },
   { "focus space 9", (const char*[]){ "<P-0>", "<P-KP_0>", NULL }, key_cb_focus_space, 9 },
   { "focus left space", (const char*[]){ "<P-Left>", NULL }, key_cb_focus_previous_space, 0 },
   { "focus right space", (const char*[]){ "<P-Right>", NULL }, key_cb_focus_next_space, 0 },
   { "move to space 0", (const char*[]){ "<P-F1>", NULL }, key_cb_move_to_space, 0 },
   { "move to space 1", (const char*[]){ "<P-F2>", NULL }, key_cb_move_to_space, 1 },
   { "move to space 2", (const char*[]){ "<P-F3>", NULL }, key_cb_move_to_space, 2 },
   { "move to space 3", (const char*[]){ "<P-F4>", NULL }, key_cb_move_to_space, 3 },
   { "move to space 4", (const char*[]){ "<P-F5>", NULL }, key_cb_move_to_space, 4 },
   { "move to space 5", (const char*[]){ "<P-F6>", NULL }, key_cb_move_to_space, 5 },
   { "move to space 6", (const char*[]){ "<P-F7>", NULL }, key_cb_move_to_space, 6 },
   { "move to space 7", (const char*[]){ "<P-F8>", NULL }, key_cb_move_to_space, 7 },
   { "move to space 8", (const char*[]){ "<P-F9>", NULL }, key_cb_move_to_space, 8 },
   { "move to space 9", (const char*[]){ "<P-F0>", NULL }, key_cb_move_to_space, 9 },
   { "move to output 0", (const char*[]){ "<P-z>", NULL }, key_cb_move_to_output, 0 },
   { "move to output 1", (const char*[]){ "<P-x>", NULL }, key_cb_move_to_output, 1 },
   { "move to output 2", (const char*[]){ "<P-c>", NULL }, key_cb_move_to_output, 2 },
   { "focus view", (const char*[]){ "<B0>", NULL }, key_cb_focus_view, 0 },
   { "move view", (const char*[]){ "<P-B0>", NULL }, key_cb_move_view, 0 },
   { "resize view", (const char*[]){ "<P-B1>", NULL }, key_cb_resize_view, 0 },
   {0},
};

static bool
setup_default_keybinds(plugin_h self)
{
   const char *terminal = getenv("TERMINAL");
   chck_string_set_cstr(&plugin.terminal, (chck_cstr_is_empty(terminal) ? DEFAULT_TERMINAL : terminal), true);

   for (size_t i = 0; keybinds[i].name; ++i)
      if (!add_keybind(self, keybinds[i].name, keybinds[i].syntax, FUN(keybinds[i].function, "v(h,u32,ip)|1"), keybinds[i].arg))
         return false;

   return true;
}

static void
load_config(plugin_h self)
{
   plugin_h configuration;
   bool (*configuration_get)(const char *key, const char type, void *value_out);
   if (!(configuration = import_plugin(self, "configuration")) ||
       !(configuration_get = import_method(self, configuration, "get", "b(c[],c,v)|1")))
      return;

   configuration_get("/core/follow-focus", 'b', &plugin.config.follow_focus);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   chck_string_release(&plugin.terminal);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h orbment, keybind, layout;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(keybind = import_plugin(self, "keybind")) ||
       !(layout = import_plugin(self, "layout")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")) ||
       !(add_keybind = import_method(self, keybind, "add_keybind", "b(h,c[],c*[],fun,ip)|1")) ||
       !(relayout = import_method(self, layout, "relayout", "v(h)|1")))
      return false;

   if (!setup_default_keybinds(self))
      return false;

   load_config(self);
   return (add_hook(self, "view.created", FUN(view_created, "b(h)|1")) &&
           add_hook(self, "view.destroyed", FUN(view_destroyed, "v(h)|1")) &&
           add_hook(self, "view.focus", FUN(view_focus, "v(h,b)|1")) &&
           add_hook(self, "view.move_to_output", FUN(view_move_to_output, "v(h,h,h)|1")) &&
           add_hook(self, "view.move_request", FUN(view_move_request, "v(h,*)|1")) &&
           add_hook(self, "view.resize_request", FUN(view_resize_request, "v(h,u32,*)|1")) &&
           add_hook(self, "pointer.motion", FUN(pointer_motion, "b(h,u32,*)|1")) &&
           add_hook(self, "pointer.button", FUN(pointer_button, "b(h,u32,*,u32,e,*)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "keybind",
      "layout",
      NULL,
   };

   static const char *after[] = {
      "configuration",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-functionality",
      .description = "Core functionality.",
      .version = VERSION,
      .requires = requires,
      .after = after,
   };

   return &info;
}
