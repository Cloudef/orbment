#include <stdlib.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/buffer/buffer.h>
#include "config.h"

static const char *compressor_signature = "u8[](p,u8[],sz*)|1";
static bool (*add_compressor)(const char *type, const char *name, const char *ext, const struct function*);
static void (*remove_compressor)(const char *type, const char *name);

static uint8_t*
compress(const struct wlc_size *size, uint8_t *rgba, size_t *out_size)
{
   if (out_size)
      *out_size = 0;

   uint8_t *rgb;
   if (!(rgb = calloc(1, size->w * size->h * 3)))
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
   if (!chck_buffer(&buf, size->w * size->h * 3, CHCK_ENDIANESS_LITTLE)) {
      free(rgb);
      return NULL;
   }

   chck_buffer_write_format(&buf, "P6\n%d %d\n255\n", size->w, size->h);
   chck_buffer_write(rgb, 1, size->w * size->h * 3, &buf);
   free(rgb);

   void *compressed = buf.buffer;
   buf.copied = false;

   if (out_size)
      *out_size = buf.size;

   chck_buffer_release(&buf);
   return compressed;
}

bool
plugin_deinit(void)
{
   remove_compressor("image", "ppm");
   return true;
}

bool
plugin_init(void)
{
   plugin_h compressor;
   if (!(compressor = import_plugin("compressor")))
      return false;

   if (!has_methods(compressor,
            (const struct method_info[]){
               METHOD("add_compressor", "b(c[],c[],c[],fun)|1"),
               METHOD("remove_compressor", "v(c[],c[])|1"),
               {0},
            }))
      return false;

   add_compressor = import_method(compressor, "add_compressor", "b(c[],c[],c[],fun)|1");
   remove_compressor = import_method(compressor, "remove_compressor", "v(c[],c[])|1");
   return add_compressor("image", "ppm", "ppm", FUN(compress, compressor_signature));
}

const struct plugin_info*
plugin_register(void)
{
   static const char *requires[] = {
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "compressor-ppm",
      .description = "Provides compression to ppm image format.",
      .version = VERSION,
      .requires = requires,
   };

   return &info;
}
