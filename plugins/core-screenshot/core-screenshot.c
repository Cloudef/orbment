#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
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

static struct {
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

static void
cb_did_compress(struct work *work)
{
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

struct info {
   size_t index;
};

static bool
cb_pixels(const struct wlc_size *dimensions, uint8_t *rgba, void *arg)
{
   struct info info;
   memcpy(&info, arg, sizeof(info));
   free(arg);

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   if (!memb || info.index >= memb) {
      plog(plugin.self, PLOG_ERROR, "Could not find compressor for index (%zu)", info.index);
      return false;
   }

   struct work work = {
      .image = {
         .data = rgba,
      },
      .dimensions = *dimensions,
      .compressor = compressors[info.index],
   };

   // if returns true, rgba is not released
   return chck_tqueue_add_task(&plugin.tqueue, &work, 0);
}

static void
key_cb_screenshot(wlc_handle view, uint32_t time, intptr_t arg)
{
   (void)view, (void)time;

   struct info *info;
   if (!(info = calloc(1, sizeof(struct info))))
      return;

   info->index = arg;
   wlc_output_get_pixels(wlc_get_focused_output(), cb_pixels, info);
}

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

   plugin_h orbment, compressor;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(compressor = import_plugin(self, "compressor")))
      return false;

   if (!(add_keybind = import_method(self, orbment, "add_keybind", "b(h,c[],c*[],fun,ip)|1")))
      return false;

   if (!(list_compressors = import_method(self, compressor, "list_compressors", "*(c[],c[],c[],sz*)|1")))
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

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
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
