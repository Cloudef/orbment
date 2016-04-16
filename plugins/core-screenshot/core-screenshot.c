#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <wlc/wlc-render.h>
#include <chck/math/math.h>
#include <chck/string/string.h>
#include <chck/thread/queue/queue.h>
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

typedef void (*keybind_fun_t)(wlc_handle view, uint32_t time, intptr_t arg);
static bool (*add_keybind)(plugin_h, const char *name, const char **syntax, const struct function*, intptr_t arg);
static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   struct {
      wlc_handle output; // if != 0, screenshot will be taken in next frame and reset after to 0
      size_t compressor;
   } action;

   struct chck_tqueue tqueue;
   plugin_h self;
} plugin;

struct image {
   uint8_t *data;
   size_t size;
};

struct work {
   struct wlc_size dimensions;
   struct compressor compressor;
   struct image image;
};

static void
work_release(struct work *work)
{
   if (!work)
      return;

   free(work->image.data);
}

PPURE static void
cb_did_compress(struct work *work)
{
   (void)work;
   assert(work);
}

static void
cb_compress(struct work *work)
{
   assert(work);

   uint8_t *data;
   if (!(data = work->compressor.function(&work->dimensions, work->image.data, &work->image.size))) {
      plog(plugin.self, PLOG_ERROR, "Failed to compress data using '%s compressor'", work->compressor.name);
      return;
   }

   free(work->image.data);
   work->image.data = data;

   if (!work->image.size)
      return;

   struct chck_string name = {0};
   time_t now;
   time(&now);
   char buf[sizeof("orbment-0000-00-00T00:00:00Z")];
   strftime(buf, sizeof(buf), "orbment-%FT%TZ", gmtime(&now));
   chck_string_set_format(&name, "%s.%s", buf, work->compressor.ext);

   FILE *f;
   if (!(f = fopen(name.data, "wb"))) {
      plog(plugin.self, PLOG_ERROR, "Could not open file for writing: %s", name.data);
      goto error1;
   }

   fwrite(data, 1, work->image.size, f);
   fclose(f);

   plog(plugin.self, PLOG_INFO, "Wrote screenshot to %s", name.data);
   chck_string_release(&name);

   return;

error1:
   chck_string_release(&name);
}

static void
key_cb_screenshot(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time;
   plugin.action.output = wlc_get_focused_output();
   plugin.action.compressor = arg;
   wlc_output_schedule_render(wlc_get_focused_output());
}

static void
output_post_render(wlc_handle output)
{
   if (plugin.action.output != output)
      return;

   plugin.action.output = 0;

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   if (!memb || plugin.action.compressor >= memb) {
      plog(plugin.self, PLOG_ERROR, "Could not find compressor for index (%zu)", plugin.action.compressor);
      return;
   }

   const struct wlc_geometry g = { .origin = { 0, 0 }, .size = *wlc_output_get_resolution(output) };

   void *rgba;
   if (!(rgba = calloc(1, g.size.w * g.size.h * 4)))
      return;

   struct wlc_geometry out;
   wlc_pixels_read(WLC_RGBA8888, &g, &out, rgba);

   struct work work = {
      .image = {
         .data = rgba,
      },
      .dimensions = out.size,
      .compressor = compressors[plugin.action.compressor],
   };

   if (!chck_tqueue_add_task(&plugin.tqueue, &work, 0))
      free(rgba);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   chck_tqueue_release(&plugin.tqueue);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h orbment, keybind, compressor;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(keybind = import_plugin(self, "keybind")) ||
       !(compressor = import_plugin(self, "compressor")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")) ||
       !(add_keybind = import_method(self, keybind, "add_keybind", "b(h,c[],c*[],fun,ip)|1")) ||
       !(list_compressors = import_method(self, compressor, "list_compressors", "*(c[],c[],c[],sz*)|1")))
      return false;

   if (!add_hook(self, "output.post_render", FUN(output_post_render, "v(h)|1")))
      return false;

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   for (size_t i = 0; i < memb; ++i) {
      struct chck_string name = {0};
      chck_string_set_format(&name, "take screenshot %s", compressors[i].name);
      const bool ret = add_keybind(self, name.data, (chck_cstreq(compressors[i].name, "png") ? (const char*[]){ "<SunPrint_Screen>", "<P-s>", NULL } : NULL), FUN(key_cb_screenshot, "v(h,u32,ip)|1"), i);
      chck_string_release(&name);

      if (!ret)
         return false;
   }

   if (!chck_tqueue(&plugin.tqueue, 1, 4, sizeof(struct work), cb_compress, cb_did_compress, work_release))
      return false;

   return true;
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "keybind",
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-screenshot",
      .description = "Screenshot functionality.",
      .version = VERSION,
      .requires = requires,
   };

   return &info;
}
