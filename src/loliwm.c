#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <wlc.h>

static struct {
   struct wlc_compositor *compositor;
} loliwm;

static void
view_created(struct wlc_compositor *compositor, struct wlc_view *view)
{
   (void)compositor;

   static int c = 0;
   wlc_view_set_state(view, (uint32_t[]){ WLC_MAXIMIZED }, 1);
   wlc_view_resize(view, 400, 480);
   wlc_view_position(view, (c ? 400 : 0), 0);
   c = !c;
}

static bool
button_press(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t button, enum wlc_button_state state)
{
   (void)button;

   if (state == WLC_BUTTON_STATE_PRESSED && button == 273) {
      // TEMPORARY UGLY
      if (fork() == 0) {
         system("weston-terminal");
         _exit(0);
      }
   }

   if (state == WLC_BUTTON_STATE_RELEASED)
      wlc_compositor_keyboard_focus(compositor, view);

   return true;
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
         .destroyed = NULL,
      },

      .pointer = {
         .button = button_press,
         .motion = NULL,
      },

      .keyboard = {
         .key = NULL,
      },
   };

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

   if (!initialize())
      return EXIT_FAILURE;

   run();
   terminate();
   return EXIT_SUCCESS;
}
