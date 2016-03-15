#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <chck/xdg/xdg.h>
#include <wlc/wlc.h>
#include "config.h"
#include "signals.h"
#include "plugin.h"
#include "hooks.h"
#include "log.h"

static void
register_plugins_from_path(void)
{
   if (chck_cstr_is_empty(PLUGINS_PATH)) {
      plog(0, PLOG_ERROR, "Could not find plugins path. PLUGINS_PATH was not set during compile.");
      return;
   }

   {
      struct chck_string xdg = {0};
      {
         char *tmp = xdg_get_path("XDG_DATA_HOME", ".local/share");
         chck_string_set_cstr(&xdg, tmp, true);
         free(tmp);
      }

      chck_string_set_format(&xdg, "%s/orbment/plugins", xdg.data);

#ifndef NDEBUG
      // allows running without install, as long as you build in debug mode
      // NOTE: $PWD/plugins is first in load order
      const char *paths[] = { "plugins", xdg.data, PLUGINS_PATH, NULL };
#else
      const char *paths[] = { xdg.data, PLUGINS_PATH, NULL };
#endif

      // FIXME: add portable directory code to chck/fs/fs.c
      for (uint32_t i = 0; paths[i]; ++i) {
         DIR *d;
         if (!(d = opendir(paths[i]))) {
            plog(0, PLOG_WARN, "Could not open plugins directory: %s", paths[i]);
            continue;
         }

         struct dirent *dir;
         while ((dir = readdir(d))) {
            if (!chck_cstr_starts_with(dir->d_name, "orbment-plugin-"))
               continue;

            struct chck_string tmp = {0};
            if (chck_string_set_format(&tmp, "%s/%s", paths[i], dir->d_name))
               plugin_register_from_path(tmp.data);
            chck_string_release(&tmp);
         }

         closedir(d);
      }

      chck_string_release(&xdg);
   }
}

static bool
setup_plugins(void)
{
   if (!hooks_setup())
      return false;

   register_plugins_from_path();
   plugin_load_all();
   return true;
}

static void
handle_arguments(int argc, char *argv[])
{
   for (int i = 1; i < argc; ++i) {
      if (chck_cstreq(argv[i], "--log")) {
         if (i + 1 >= argc) {
            plog(0, PLOG_ERROR, "--log takes an argument (filename)");
            abort();
         }
         log_set_file(argv[++i]);
      }
   }
}

int
main(int argc, char *argv[])
{
   (void)argc, (void)argv;

   signals_setup_debug();

   // XXX: Potentially dangerous under suid
   handle_arguments(argc, argv);
   log_open();

   if (!wlc_init(hooks_get_interface(), argc, argv))
      return EXIT_FAILURE;

   signals_setup();

   if (!setup_plugins())
      return EXIT_FAILURE;

   plog(0, PLOG_INFO, "-- Orbment started --");

   wlc_run();

   plog(0, PLOG_INFO, "-- Orbment is gone, bye bye! --");
   log_close();
   return EXIT_SUCCESS;
}
