#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <wlc.h>
#include <wayland-util.h>

static struct {
   struct wlc_compositor *compositor;
   struct wlc_view *active;
} loliwm;

static void
relayout(struct wlc_space *space)
{
   struct wl_list *views;
   if (!(views = wlc_space_get_userdata(space)))
      return;

   uint32_t rwidth, rheight;
   struct wlc_output *output = wlc_space_get_output(space);
   wlc_output_get_resolution(output, &rwidth, &rheight);

   struct wlc_view *v;
   uint32_t count = 0;
   wlc_view_for_each_user(v, views) {
      if (!(wlc_view_get_state(v) & WLC_BIT_FULLSCREEN))
         ++count;
   }

   bool toggle = false;
   uint32_t y = 0, height = rheight / (count > 1 ? count - 1 : 1);
   wlc_view_for_each_user(v, views) {
      if ((wlc_view_get_state(v) & WLC_BIT_FULLSCREEN)) {
         wlc_view_resize(v, rwidth, rheight);
         wlc_view_position(v, 0, 0);
         continue;
      }

      wlc_view_set_maximized(v, true);
      wlc_view_resize(v, (count > 1 ? rwidth / 2 : rwidth), (toggle ? height : rheight));
      wlc_view_position(v, (toggle ? rwidth / 2 : 0), y);

      if (toggle)
         y += height;

      toggle = true;
   }
}

static void
cycle(struct wlc_compositor *compositor)
{
   struct wl_list *l = wlc_space_get_userdata(wlc_compositor_get_focused_space(compositor));
   if (!l || wl_list_empty(l))
      return;

   struct wl_list *p = l->prev;
   wl_list_remove(l->prev);
   wl_list_insert(l, p);

   relayout(wlc_compositor_get_focused_space(compositor));
}

static void
set_active(struct wlc_view *view)
{
   if (loliwm.active == view)
      return;

   if (loliwm.active)
      wlc_view_set_active(loliwm.active, false);

   if (view) {
      struct wlc_view *v;
      struct wl_list *views = wlc_space_get_views(wlc_view_get_space(view));
      wlc_view_for_each_reverse(v, views) {
         if ((wlc_view_get_state(v) & WLC_BIT_FULLSCREEN)) {
            // Bring the first topmost found fullscreen wlc_view to front.
            // This way we get a "peek" effect when we cycle other views.
            // Meaning the active view is always over fullscreen view,
            // but fullscreen view is on top of the other views.
            wlc_view_bring_to_front(v);
            break;
         }
      }

      wlc_view_set_active(view, true);
      wlc_view_bring_to_front(view);
   }

   loliwm.active = view;
}

static void
focus_next_view(struct wlc_compositor *compositor, struct wlc_view *view)
{
   struct wl_list *l = wlc_view_get_user_link(view)->next;
   struct wl_list *views = wlc_space_get_userdata(wlc_view_get_space(view));
   if (!l || wl_list_empty(views) || (l == views && !(l = l->next)))
      return;

   struct wlc_view *v;
   if (!(v = wlc_view_from_user_link(l)))
      return;

   wlc_compositor_focus_view(compositor, v);
   set_active(v);
}

static void
focus_next_space(struct wlc_compositor *compositor)
{
   struct wlc_space *active = wlc_compositor_get_focused_space(compositor);
   struct wl_list *l = wlc_space_get_link(active)->next;
   struct wl_list *spaces = wlc_output_get_spaces(wlc_space_get_output(active));
   if (!l || wl_list_empty(spaces) || (l == spaces && !(l = l->next)))
      return;

   struct wlc_space *s;
   if (!(s = wlc_space_from_link(l)))
      return;

   wlc_output_focus_space(wlc_space_get_output(s), s);
}

static void
focus_next_output(struct wlc_compositor *compositor)
{
   struct wlc_output *active = wlc_compositor_get_focused_output(compositor);
   struct wl_list *l = wlc_output_get_link(active)->next;
   struct wl_list *outputs = wlc_compositor_get_outputs(compositor);
   if (!l || wl_list_empty(outputs) || (l == outputs && !(l = l->next)))
      return;

   struct wlc_output *o;
   if (!(o = wlc_output_from_link(l)))
      return;

   wlc_compositor_focus_output(compositor, o);
}

static bool
view_created(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   struct wl_list *views;
   struct wlc_space *space = wlc_view_get_space(view);
   if (!(views = wlc_space_get_userdata(space))) {
      if (!(views = calloc(1, sizeof(struct wl_list))))
         return false;

      wl_list_init(views);
      wlc_space_set_userdata(space, views);
   }

   wl_list_insert(views->prev, wlc_view_get_user_link(view));
   set_active(view);
   relayout(space);
   printf("NEW VIEW: %p\n", view);
   return true;
}

static void
view_destroyed(struct wlc_compositor *compositor, struct wlc_view *view)
{
   struct wl_list *views = wlc_space_get_userdata(wlc_view_get_space(view));
   wl_list_remove(wlc_view_get_user_link(view));

   if (loliwm.active == view) {
      loliwm.active = NULL;

      struct wlc_view *v;
      if (!wl_list_empty(views) && (v = wlc_view_from_user_link(views->prev))) {
         wlc_compositor_focus_view(compositor, v);
         set_active(v);
      }
   }

   relayout(wlc_view_get_space(view));

   if (wl_list_empty(views)) {
      free(views);
      wlc_space_set_userdata(wlc_view_get_space(view), NULL);
   }

   printf("VIEW DESTROYED: %p\n", view);
}

static void
view_move(struct wlc_compositor *compositor, struct wlc_view *view, int32_t x, int32_t y)
{
   (void)compositor;
   wlc_view_position(view, x, y);
   wlc_view_set_maximized(view, false);
}

static bool
pointer_button(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t button, enum wlc_button_state state)
{
   (void)button;

   if (state == WLC_BUTTON_STATE_PRESSED) {
      wlc_compositor_focus_view(compositor, view);
      set_active(view);
   }

   return true;
}

static void
keyboard_init(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;
   wlc_compositor_focus_view(compositor, view);
   printf("KEYBOARD INIT: %p\n", view);
}

static bool
keyboard_key(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t leds, uint32_t mods, uint32_t key, enum wlc_key_state state)
{
   (void)leds;

   bool pass = true;
   if (mods & WLC_BIT_MOD_ALT) {
      if (view && key == 16) {
         if (state == WLC_KEY_STATE_RELEASED)
            wlc_view_close(view);
         pass = false;
      } else if (key == 28) {
         if (state == WLC_KEY_STATE_RELEASED) {
            // TEMPORARY UGLY
            if (fork() == 0) {
               const char *terminal = getenv("TERMINAL");
               terminal = (terminal ? terminal : "weston-terminal");
               execlp(terminal, terminal, NULL);
               _exit(0);
            }
         }
         pass = false;
      } else if (key == 35) {
         if (state == WLC_KEY_STATE_RELEASED)
            cycle(compositor);
         pass = false;
      } else if (key == 36) {
         if (state == WLC_KEY_STATE_RELEASED)
            focus_next_space(compositor);
         pass = false;
      } else if (key == 37) {
         if (state == WLC_KEY_STATE_RELEASED)
            focus_next_output(compositor);
         pass = false;
      } else if (view && key == 38) {
         if (state == WLC_KEY_STATE_RELEASED)
            focus_next_view(compositor, view);
         pass = false;
      }
   }

   if (mods & WLC_BIT_MOD_CTRL && key == 16) {
      if (state == WLC_KEY_STATE_RELEASED)
         exit(EXIT_SUCCESS);
   }

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
      wlc_compositor_focus_view(compositor, wlc_view_from_link(views->prev));
      set_active(wlc_view_from_link(views->prev));
   } else {
      wlc_compositor_focus_view(compositor, NULL);
      set_active(NULL);
   }
}

static void
space_notify(struct wlc_compositor *compositor, struct wlc_space *space)
{
   struct wl_list *views = wlc_space_get_views(space);

   if (!wl_list_empty(views)) {
      wlc_compositor_focus_view(compositor, wlc_view_from_link(views->prev));
      set_active(wlc_view_from_link(views->prev));
   } else {
      wlc_compositor_focus_view(compositor, NULL);
      set_active(NULL);
   }
}

static void
output_created(struct wlc_compositor *compositor, struct wlc_output *output)
{
   (void)compositor;
   wlc_space_add(output); // add second space
}

static void
terminate(void)
{
   if (loliwm.compositor)
      wlc_compositor_free(loliwm.compositor);

   memset(&loliwm, 0, sizeof(loliwm));
}

static bool
initialize(void)
{
   struct wlc_interface interface = {
      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .move = view_move,
      },

      .pointer = {
         .button = pointer_button,
      },

      .keyboard = {
         .init = keyboard_init,
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

static
void run(void)
{
   fprintf(stdout, "-!- loliwm started\n");
   wlc_compositor_run(loliwm.compositor);
}

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   if (!wlc_init(argc, argv))
      return EXIT_FAILURE;

   if (!initialize())
      return EXIT_FAILURE;

   run();
   terminate();
   return EXIT_SUCCESS;
}
