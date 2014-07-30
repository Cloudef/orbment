#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <wlc.h>

static struct {
   struct wlc_compositor *compositor;
} loliwm;

#if 0
static
void quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
   (void)data, (void)time, (void)value;

   if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
      return;

   if (loliwm.compositor.display)
      wl_display_terminate(loliwm.compositor.display);
}
#endif

static void
setup_keys(void)
{
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

   setup_keys();
   run();
   terminate();
   return EXIT_SUCCESS;
}
