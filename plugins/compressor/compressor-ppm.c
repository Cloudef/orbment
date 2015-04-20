#include <stdlib.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/buffer/buffer.h>
#include <chck/overflow/overflow.h>
#include "config.h"

static bool (*add_compressor)(plugin_h, const char *type, const char *name, const char *ext, const struct function*);

static uint8_t*
compress_ppm(const struct wlc_size *size, uint8_t *rgba, size_t *out_size)
{
   if (out_size)
      *out_size = 0;

   if (!size || !size->w || !size->h)
      return NULL;

   size_t sz;
   uint8_t *rgb;
   if (chck_mul_ofsz(size->w, size->h, &sz) || !(rgb = calloc(3, sz)))
      return NULL;

   for (uint32_t i = 0, c = 0; i < size->w * size->h * 4; i += 4, c += 3)
      memcpy(rgb + c, rgba + i, 3);

   for (uint32_t i = 0; i * 2 < size->h; ++i) {
      uint32_t o = i * size->w * 3;
      uint32_t r = (size->h - 1 - i) * size->w * 3;
      for (uint32_t i2 = size->w * 3; i2 > 0; --i2, ++o, ++r) {
         uint8_t temp = rgb[o];
         rgb[o] = rgb[r];
         rgb[r] = temp;
      }
   }

   struct chck_buffer buf;
   if (!chck_buffer(&buf, size->w * size->h * 3, CHCK_ENDIANESS_LITTLE))
      goto error0;

   chck_buffer_write_format(&buf, "P6\n%d %d\n255\n", size->w, size->h);
   chck_buffer_write(rgb, 1, size->w * size->h * 3, &buf);
   free(rgb);

   void *compressed = buf.buffer;
   buf.copied = false;

   if (out_size)
      *out_size = buf.curpos - buf.buffer;

   chck_buffer_release(&buf);
   return compressed;

error0:
   free(rgb);
   return NULL;
}

bool
plugin_init(plugin_h self)
{
   plugin_h compressor;
   if (!(compressor = import_plugin(self, "compressor")))
      return false;

   if (!(add_compressor = import_method(self, compressor, "add_compressor", "b(h,c[],c[],c[],fun)|1")))
      return false;

   return add_compressor(self, "image", "ppm", "ppm", FUN(compress_ppm, "u8[](p,u8[],sz*)|1"));
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "compressor",
      NULL,
   };

   static const char *groups[] = {
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "compressor-ppm",
      .description = "Compression to ppm image format.",
      .version = VERSION,
      .requires = requires,
      .groups = groups,
   };

   return &info;
}
