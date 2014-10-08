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
relayout(struct wlc_output *output)
{
   struct wl_list *views;
   if (!(views = wlc_output_get_userdata(output)))
      return;

   uint32_t rwidth, rheight;
   wlc_output_get_resolution(output, &rwidth, &rheight);

   uint32_t count = wl_list_length(views);
   uint32_t y = 0, height = rheight / (count > 1 ? count - 1 : 1);

   bool toggle = false;
   struct wlc_view *v;
   wlc_view_for_each_user(v, views) {
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
   struct wl_list *l = wlc_output_get_userdata(wlc_compositor_get_focused_output(compositor));
   if (wl_list_empty(l))
      return;

   struct wl_list *p = l->prev;
   wl_list_remove(l->prev);
   wl_list_insert(l, p);

   relayout(wlc_compositor_get_focused_output(compositor));
}

static void
set_active(struct wlc_view *view)
{
   if (loliwm.active == view)
      return;

   if (loliwm.active)
      wlc_view_set_active(loliwm.active, false);

   if (view) {
      wlc_view_set_active(view, true);
      wlc_view_bring_to_front(view);
   }

   loliwm.active = view;
}

static bool
view_created(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   struct wl_list *views;
   struct wlc_output *output = wlc_view_get_output(view);
   if (!(views = wlc_output_get_userdata(output))) {
      if (!(views = calloc(1, sizeof(struct wl_list))))
         return false;

      wl_list_init(views);
      wlc_output_set_userdata(output, views);
   }

   wl_list_insert(views->prev, wlc_view_get_user_link(view));
   set_active(view);
   relayout(output);
   printf("NEW VIEW: %p\n", view);
   return true;
}

static void
view_destroyed(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   struct wl_list *views = wlc_output_get_userdata(wlc_view_get_output(view));
   wl_list_remove(wlc_view_get_user_link(view));

   if (loliwm.active == view) {
      loliwm.active = NULL;

      struct wlc_view *v;
      if (!wl_list_empty(views) && (v = wlc_view_from_user_link(views->prev))) {
         wlc_compositor_keyboard_focus(compositor, v);
         set_active(v);
      }
   }

   relayout(wlc_view_get_output(view));

   if (wl_list_empty(views)) {
      free(views);
      wlc_output_set_userdata(wlc_view_get_output(view), NULL);
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
      wlc_compositor_keyboard_focus(compositor, view);
      set_active(view);
   }

   return true;
}

static void
keyboard_init(struct wlc_compositor *compositor, struct wlc_view *view)
{
   wlc_compositor_keyboard_focus(compositor, view);
   printf("KEYBOARD INIT: %p\n", view);
}

static void
focus_next_view(struct wlc_compositor *compositor, struct wlc_view *view)
{
   struct wl_list *l = wlc_view_get_user_link(view)->next;
   struct wl_list *views = wlc_output_get_userdata(wlc_view_get_output(view));
   if (!l || wl_list_empty(views) || (l == views && !(l = l->next)))
      return;

   struct wlc_view *v;
   if (!(v = wlc_view_from_user_link(l)))
      return;

   wlc_compositor_keyboard_focus(compositor, v);
   set_active(v);
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

   wlc_compositor_output_focus(compositor, o);
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
               execlp("weston-terminal", "weston-terminal", NULL);
               _exit(0);
            }
         }
         pass = false;
      } else if (key == 35) {
         if (state == WLC_KEY_STATE_RELEASED)
            cycle(compositor);
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
   relayout(output);
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
         .resolution = resolution_notify,
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
main(int argc, char **argv)
{
   (void)argc, (void)argv;

   if (!wlc_init())
      return EXIT_FAILURE;

   if (!initialize())
      return EXIT_FAILURE;

   run();
   terminate();
   return EXIT_SUCCESS;
}
