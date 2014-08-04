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
      wlc_view_set_state(v, (uint32_t[]){ WLC_MAXIMIZED }, 1);
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

   if (view)
      wlc_view_set_active(view, true);

   loliwm.active = view;
}

static void
view_created(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;
   wl_list_insert((wl_list_length(&loliwm.views) > 0 ? loliwm.views.prev : &loliwm.views), wlc_view_get_link(view));
   set_active(view);
   relayout();
}

static void
view_destroyed(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   wl_list_remove(wlc_view_get_link(view));

   if (loliwm.active == view) {
      loliwm.active = NULL;

      struct wlc_view *v;
      if (wl_list_length(&loliwm.views) > 0 && (v = wlc_view_from_link(loliwm.views.prev))) {
         wlc_compositor_keyboard_focus(compositor, v);
         set_active(v);
      }
   }

   relayout();
}

static bool
button_press(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t button, enum wlc_button_state state)
{
   (void)button;

   if (state == WLC_BUTTON_STATE_RELEASED && button == 273) {
      // TEMPORARY UGLY
      if (fork() == 0) {
         system("weston-terminal");
         _exit(0);
      }
   } else if (state == WLC_BUTTON_STATE_RELEASED) {
      wlc_compositor_keyboard_focus(compositor, view);
      set_active(view);
   }

   return true;
}

static void
keyboard_init(struct wlc_compositor *compositor, struct wlc_view *view)
{
   wlc_compositor_keyboard_focus(compositor, view);
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
      },

      .pointer = {
         .button = button_press,
         .motion = NULL,
      },

      .keyboard = {
         .init = keyboard_init,
         .key = NULL,
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
