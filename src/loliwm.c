#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <wlc.h>

static struct {
   struct wlc_compositor *compositor;
} loliwm;


static bool
button_press(struct wlc_compositor *compositor, struct wlc_view *view, uint32_t button, enum wlc_button_state state)
{
   (void)button, (void)state;

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
         .created = NULL,
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
