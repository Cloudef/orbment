#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include <pthread.h>
#include "config.h"

static const char *compress_signature = "u8[](p,u8[],sz*)|1";
typedef uint8_t* (*compress_fun)(const struct wlc_size*, uint8_t*, size_t*);

static const char *struct_signature = "c[],c[],*|1";
struct compressor {
   const char *name;
   const char *ext;
   compress_fun function;
};

struct compressor* (*list_compressors)(const char *type, const char *stsign, const char *funsign, size_t *out_memb);

static const char *keybind_signature = "v(h,u32,ip)|1";
typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(const char *name, const char *syntax, const struct function*, intptr_t arg);
static void (*remove_keybind)(const char *name);

static struct chck_iter_pool keybinds;

struct work {
   struct wlc_size dimensions;
   struct compressor compressor;
   uint8_t *data;
};

static void*
on_thread(void *arg)
{
   struct work *work = arg;
   assert(work);

   if (!work)
      return NULL;

   time_t now;
   time(&now);
   char buf[sizeof("orbment-0000-00-00T00:00:00Z")];
   strftime(buf, sizeof(buf), "orbment-%FT%TZ", gmtime(&now));

   size_t size;
   uint8_t *data;
   if (!(data = work->compressor.function(&work->dimensions, work->data, &size)) || !size) {
      wlc_log(WLC_LOG_ERROR, "Failed to compress data using '%s compressor'", work->compressor.name);
      goto error0;
   }

   struct chck_string name = {0};
   chck_string_set_format(&name, "%s.%s", buf, work->compressor.ext);

   FILE *f;
   if (!(f = fopen(name.data, "wb"))) {
      wlc_log(WLC_LOG_ERROR, "Could not open file for writing: %s", name.data);
      goto error1;
   }

   fwrite(data, 1, size, f);
   free(data);
   fclose(f);

   wlc_log(WLC_LOG_INFO, "Wrote screenshot to %s", name.data);
   chck_string_release(&name);
   free(work->data);
   free(work);
   return NULL;

error1:
   chck_string_release(&name);
   free(data);
error0:
   free(work->data);
   free(work);
   return NULL;
}

static bool
cb_pixels(const struct wlc_size *dimensions, uint8_t *rgba, void *arg)
{
   size_t memb;
   size_t i = (size_t)arg;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   if (!memb || i >= memb) {
      wlc_log(WLC_LOG_ERROR, "Could not find compressor for index (%zu)", i);
      return false;
   }

   struct work *work;
   if (!(work = calloc(1, sizeof(struct work)))) {
      wlc_log(WLC_LOG_ERROR, "Out of memory");
      return false;
   }

   work->data = rgba;
   work->dimensions = *dimensions;
   memcpy(&work->compressor, &compressors[i], sizeof(struct compressor));

   pthread_t thread;
   if (pthread_create(&thread, NULL, on_thread, work) != 0) {
      wlc_log(WLC_LOG_ERROR, "Could not spawn thread");
      return false;
   }

   pthread_detach(thread);
   return true; // ← rgba is not released
}

static void
key_cb_screenshot(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time;
   wlc_output_get_pixels(wlc_get_focused_output(), cb_pixels, (void*)arg);
}

bool
plugin_deinit(void)
{
   struct chck_string *str;
   chck_iter_pool_for_each(&keybinds, str)
      remove_keybind(str->data);

   chck_iter_pool_for_each_call(&keybinds, chck_string_release);
   chck_iter_pool_release(&keybinds);
   return true;
}

bool
plugin_init(void)
{
   plugin_h orbment, compressor;
   if (!(orbment = import_plugin("orbment")) ||
       !(compressor = import_plugin("compressor")))
      return false;

   if (!(add_keybind = import_method(orbment, "add_keybind", "b(c[],c[],fun,ip)|1")) ||
       !(remove_keybind = import_method(orbment, "remove_keybind", "v(c[])|1")))
      return false;

   if (!(list_compressors = import_method(compressor, "list_compressors", "*(c[],c[],c[],sz*)|1")))
      return false;

   if (!chck_iter_pool(&keybinds, 4, 0, sizeof(struct chck_string)))
      return false;

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   for (size_t i = 0; i < memb; ++i) {
      struct chck_string name = {0};
      chck_string_set_format(&name, "take screenshot (%s)", compressors[i].name);
      if (!add_keybind(name.data, (chck_cstreq(compressors[i].name, "png") ? "<P-s>" : NULL), FUN(key_cb_screenshot, keybind_signature), i))
         return false;
      chck_iter_pool_push_back(&keybinds, &name);
   }

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-screenshot",
      .description = "Provides screenshot functionality.",
      .version = VERSION,
      .requires = requires,
   };

   return &info;
}
