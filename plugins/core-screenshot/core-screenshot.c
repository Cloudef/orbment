#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/math/math.h>
#include <chck/string/string.h>
#include <chck/lut/lut.h>
#include <chck/thread/queue/queue.h>
#include <sys/eventfd.h>
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

static bool (*add_node)(plugin_h, const char *container, const char *path, const char *type, void *arg, const struct function *read, const struct function *write, const struct function *clunk, const struct function *size);
static bool (*reply)(uint16_t tag, const void *src, size_t size, size_t nmemb, void *stream);

static bool (*add_hook)(plugin_h, const char *name, const struct function*);

static struct {
   struct chck_tqueue tqueue;
   struct chck_lut reads; // for vfs
   struct wlc_event_source *source; // for vfs
   plugin_h self;
} plugin;

struct read {
   void *stream;
   uint32_t fid;
   uint16_t tag;
   uint64_t offset;
   uint32_t count;
};

struct image {
   uint8_t *data;
   size_t size;
};

struct work {
   struct wlc_size dimensions;
   struct compressor compressor;
   struct read read;
   struct image image;
   uint16_t tag;
};

static bool cb_screenshot_read(void *arg, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, void *stream);

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

   if (work->read.stream) {
      struct image *image;
      if ((image = chck_lut_get(&plugin.reads, work->read.fid)) && image->data)
         return;

      if (chck_lut_set(&plugin.reads, work->read.fid, &work->image)) {
         cb_screenshot_read(NULL, work->read.tag, work->read.fid, work->read.offset, work->read.count, work->read.stream);
         memset(&work->image, 0, sizeof(work->image)); // ← we do not want the data to be freed now
      }
   }
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
   if (!work->read.stream) {
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
   }

   return;

error1:
   chck_string_release(&name);
}

struct info {
   struct read read;
   size_t index;
};

static bool
cb_pixels(const struct wlc_size *dimensions, uint8_t *rgba, void *arg)
{
   struct info info;
   memmove(&info, arg, sizeof(info));
   free(arg);

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   if (!memb || info.index >= memb) {
      plog(plugin.self, PLOG_ERROR, "Could not find compressor for index (%zu)", info.index);
      return false;
   }

   struct work work = {
      .read = info.read,
      .image = {
         .data = rgba,
      },
      .dimensions = *dimensions,
      .compressor = compressors[info.index],
   };

   // if returns true, rgba is not released
   return chck_tqueue_add_task(&plugin.tqueue, &work, 0);
}

static bool
cb_screenshot_read(void *arg, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, void *stream)
{
   (void)offset, (void)count;

   const struct image *image;
   if ((image = chck_lut_get(&plugin.reads, fid)) && image->data) {
      offset = chck_minsz(image->size, offset);
      count = chck_minsz(image->size - offset, count);
      return (reply ? reply(tag, image->data + offset, 1, count, stream) : false);
   }

   if (!arg)
      return false;

   struct info *info;
   if (!(info = calloc(1, sizeof(struct info))))
      return false;

   info->index = (size_t)arg - 1;
   info->read.stream = stream;
   info->read.tag = tag;
   info->read.fid = fid;
   info->read.offset = offset;
   info->read.count = count;
   wlc_output_get_pixels(wlc_get_focused_output(), cb_pixels, info);
   return true;
}

static void
cb_screenshot_clunk(void *arg, uint32_t fid)
{
   (void)arg;

   struct image *image;
   if ((image = chck_lut_get(&plugin.reads, fid)) && image->data)
      free(image->data);

   chck_lut_set(&plugin.reads, fid, NULL);
}

static int
cb_collect(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask, (void)data;
   chck_tqueue_collect(&plugin.tqueue);
   return 0;
}

static bool
add_to_event_loop(void)
{
   int fd;
   if ((fd = eventfd(0, EFD_CLOEXEC)) < 0)
      return false;

   chck_tqueue_set_fd(&plugin.tqueue, fd);
   close(fd);

   if (!(plugin.source = wlc_event_loop_add_fd(chck_tqueue_get_fd(&plugin.tqueue), WLC_EVENT_READABLE, cb_collect, NULL)))
      return false;

   return true;
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

static bool
output_created(wlc_handle output)
{
   if (!add_node)
      return true;

   size_t memb;
   struct compressor *compressors = list_compressors("image", struct_signature, compress_signature, &memb);
   for (size_t i = 0; i < memb; ++i) {
      struct chck_string tmp = {0};
      if ((chck_string_set_format(&tmp, "screenshot.%s", compressors[i].ext))) {
         add_node(plugin.self, wlc_output_get_name(output), tmp.data, "file", (void*)(i + 1), FUN(cb_screenshot_read, "b(*,u16,u32,u64,u32,*)|1"), NULL, FUN(cb_screenshot_clunk, "v(*,u32)|1"), NULL);
         chck_string_release(&tmp);
      }
   }

   return true;
}

static bool
vfs_init(plugin_h self)
{
   plugin_h vfs;
   if (!(vfs = import_plugin(self, "vfs")))
      return false;

   if (!(add_node = import_method(self, vfs, "add_node", "b(h,c[],c[],c[],*,fun,fun,fun,fun)|1")) ||
       !(reply = import_method(self, vfs, "reply", "b(u16,*,sz,sz,*)|1")))
      goto fail;

   return true;

fail:
   add_node = NULL;
   reply = NULL;
   return false;
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;
   chck_tqueue_release(&plugin.tqueue);

   struct image *image;
   chck_lut_for_each(&plugin.reads, image)
      free(image->data);

   chck_lut_release(&plugin.reads);

   if (plugin.source)
      wlc_event_source_remove(plugin.source);
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;
   vfs_init(self);

   plugin_h orbment, keybind, compressor;
   if (!(orbment = import_plugin(self, "orbment")) ||
       !(keybind = import_plugin(self, "keybind")) ||
       !(compressor = import_plugin(self, "compressor")))
      return false;

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")) ||
       !(add_keybind = import_method(self, keybind, "add_keybind", "b(h,c[],c*[],fun,ip)|1")) ||
       !(list_compressors = import_method(self, compressor, "list_compressors", "*(c[],c[],c[],sz*)|1")))
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

   if (!chck_lut(&plugin.reads, 0, 4, sizeof(struct image)))
      return false;

   if (!chck_tqueue(&plugin.tqueue, 1, 4, sizeof(struct work), cb_compress, cb_did_compress, work_release))
      return false;

   if (!add_to_event_loop())
      return false;

   return (!add_node || add_hook(self, "output.created", FUN(output_created, "b(h)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "keybind",
      "compressor",
      NULL,
   };

   static const char *after[] = {
      "vfs",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "core-screenshot",
      .description = "Screenshot functionality.",
      .version = VERSION,
      .requires = requires,
      .after = after,
   };

   return &info;
}
