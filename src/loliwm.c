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
} loliwm;

static void
relayout(void)
{
   struct wlc_view *v;
   bool toggle = false;
   uint32_t count = wl_list_length(&loliwm.views);
   uint32_t y = 0, height = 480 / (count > 1 ? count - 1 : 1);
   wlc_view_for_each(v, &loliwm.views) {
      wlc_view_set_maximized(v, true);
      wlc_view_resize(v, (count > 1 ? 400 : 800), (toggle ? height : 480));
      wlc_view_position(v, (toggle ? 400 : 0), y);

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
view_move(struct wlc_compositor *compositor, struct wlc_view *view, float x, float y)
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

static bool
keyboard_key(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t leds, uint32_t mods, uint32_t key, enum wlc_key_state state)
{
   (void)leds;

   bool pass = true;
   if (mods & WLC_BIT_MOD_ALT) {
      if (view && key == 38) {
         if (state == WLC_KEY_STATE_RELEASED) {
            struct wlc_view *v;
            struct wl_list *l = wlc_view_get_link(view)->next;
            if ((v = wlc_view_from_link((l == &loliwm.views ? l->next : l)))) {
               wlc_compositor_keyboard_focus(compositor, v);
               set_active(v);
            }
         }
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
   };

   wlc_compositor_inject(loliwm.compositor, &interface);
   wl_list_init(&loliwm.views);
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

   if (!initialize())
      return EXIT_FAILURE;

   run();
   terminate();
   return EXIT_SUCCESS;
}
