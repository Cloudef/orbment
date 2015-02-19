#include <loliwm/plugin.h>
#include <stdio.h>
#include <assert.h>

static void
hello_world(void)
{
   printf("hello world\n");
}

bool
plugin_deinit(void)
{
   return true;
}

bool
plugin_init(void)
{
   plugin_h loliwm;
   if (!(loliwm = import_plugin("loliwm")))
      return false;

   assert(loliwm);
   assert(!import_plugin("do_not_found"));

   void (*do_nothing)(void);
   if (!(do_nothing = import_method(loliwm, "do_nothing", "v(v)|1")))
      return false;

   assert(do_nothing);
   assert(!import_method(loliwm, "do_nothing", "v(v)|0")); // should warn as well
   assert(!import_method(loliwm, "do_not_found", "v(v)|1")); // should warn as well

   do_nothing();
   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const char* provides[] = { "test", NULL };

   static const char* requires[] = { "loliwm", NULL };

   static const struct method methods[] = {
      REGISTER_METHOD(hello_world, "v(v)|1"),
      {0},
   };

   static const struct plugin_info info = {
      .name = "test",
      .version = "1.0.0",

      // these are in addittion to name just for show. Useless in this case.
      .provides = provides,
      .conflicts = provides,

      // "loliwm" is always available, just for show here
      .requires = requires,

      // Core plugins are always loaded first, just for show,
      // also requires has same effect as after, expect after can be used for optional plugins
      .after = requires,

      // All the methods this plugin exports for other plugins.
      .methods = methods,
   };

   return &info;
}
