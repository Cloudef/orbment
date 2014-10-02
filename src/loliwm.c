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
   struct wl_list views;
   uint32_t width, height;
} loliwm;

static void
relayout(void)
{
   struct wlc_view *v;
   bool toggle = false;
   uint32_t count = wl_list_length(&loliwm.views);
   uint32_t y = 0, height = loliwm.height / (count > 1 ? count - 1 : 1);
   wlc_view_for_each(v, &loliwm.views) {
      wlc_view_set_maximized(v, true);
      wlc_view_resize(v, (count > 1 ? loliwm.width / 2 : loliwm.width), (toggle ? height : loliwm.height));
      wlc_view_position(v, (toggle ? loliwm.width / 2 : 0), y);

      if (toggle)
         y += height;

      toggle = true;
   }
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

static void
view_created(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;
   wl_list_insert(loliwm.views.prev, wlc_view_get_link(view));
   set_active(view);
   relayout();
   printf("NEW VIEW: %p\n", view);
}

static void
view_destroyed(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   wl_list_remove(wlc_view_get_link(view));

   if (loliwm.active == view) {
      loliwm.active = NULL;

      struct wlc_view *v;
      if (!wl_list_empty(&loliwm.views) && (v = wlc_view_from_link(loliwm.views.prev))) {
         wlc_compositor_keyboard_focus(compositor, v);
         set_active(v);
      }
   }

   relayout();
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
focus_next(struct wlc_compositor *compositor, struct wlc_view *view)
{
   struct wl_list *l = wlc_view_get_link(view)->next;
   if (!l || wl_list_empty(&loliwm.views) || (l == &loliwm.views && !(l = l->next)))
      return;

   struct wlc_view *v;
   if (!(v = wlc_view_from_link(l)))
      return;

   wlc_compositor_keyboard_focus(compositor, v);
   set_active(v);
}

static bool
keyboard_key(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t leds, uint32_t mods, uint32_t key, enum wlc_key_state state)
{
   (void)leds;

   bool pass = true;
   if (mods & WLC_BIT_MOD_ALT) {
      if (view && key == 38) {
         if (state == WLC_KEY_STATE_RELEASED)
            focus_next(compositor, view);
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
      } else if (view && key == 16) {
         if (state == WLC_KEY_STATE_RELEASED)
            wlc_view_close(view);
         pass = false;
      }
   }

   printf("(%p) KEY: %u\n", view, key);
   return pass;
}

static void
resolution_notify(struct wlc_compositor *compositor, uint32_t width, uint32_t height)
{
   (void)compositor;
   loliwm.width = width;
   loliwm.height = height;
   relayout();
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
   if (!(loliwm.compositor = wlc_compositor_new()))
      goto fail;

   struct wlc_interface interface = {
      .view = {
         .created = view_created,
         .destroyed = view_destroyed,
         .move = view_move,
      },

      .pointer = {
         .button = pointer_button,
         .motion = NULL,
      },

      .keyboard = {
         .init = keyboard_init,
         .key = keyboard_key,
      },

      .output = {
         .resolution = resolution_notify,
      },
   };

   wl_list_init(&loliwm.views);
   wlc_compositor_inject(loliwm.compositor, &interface);
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
