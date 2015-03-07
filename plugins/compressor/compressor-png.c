#include <stdlib.h>
#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/buffer/buffer.h>
#include <png.h>
#include "config.h"

static const char *compressor_signature = "u8[](p,u8[],sz*)|1";
static bool (*add_compressor)(const char *type, const char *name, const char *ext, const struct function*);
static void (*remove_compressor)(const char *type, const char *name);

static void
write(png_structp p, png_bytep data, png_size_t length)
{
   assert(p);
   struct chck_buffer *buf = (struct chck_buffer*)png_get_io_ptr(p);
   assert(buf);
   chck_buffer_write(data, 1, length, buf);
}

static uint8_t*
compress(const struct wlc_size *size, uint8_t *rgba, size_t *out_size)
{
   if (out_size)
      *out_size = 0;

   if (!size || !size->w || !size->h)
      return NULL;

   png_structp p;
   if (!(p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
      return NULL;

   png_infop info;
   if (!(info = png_create_info_struct(p)))
      goto error0;

   struct chck_buffer buf;
   if (!(chck_buffer(&buf, 8192, CHCK_ENDIANESS_LITTLE)))
      goto error1;

   buf.step = 8192;

   png_bytepp rows;
   if (!(rows = malloc(size->h * sizeof(png_bytep))))
      goto error2;

   png_set_IHDR(p, info, size->w, size->h, 8,
         PNG_COLOR_TYPE_RGBA,
         PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_DEFAULT,
         PNG_FILTER_TYPE_DEFAULT);

   for (size_t y = 0; y < size->h; ++y)
      rows[y] = rgba + ((size->h - 1) - y) * size->w * 4;

   png_set_rows(p, info, rows);
   png_set_write_fn(p, &buf, write, NULL);
   png_write_png(p, info, PNG_TRANSFORM_IDENTITY, NULL);
   free(rows);
   png_destroy_info_struct(p, &info);
   png_destroy_write_struct(&p, NULL);

   void *compressed = buf.buffer;
   buf.copied = false;

   if (out_size)
      *out_size = buf.curpos - buf.buffer;

   chck_buffer_release(&buf);
   return compressed;

error2:
   chck_buffer_release(&buf);
error1:
   png_destroy_info_struct(p, &info);
error0:
   png_destroy_write_struct(&p, NULL);
   return NULL;
}

bool
plugin_deinit(void)
{
   remove_compressor("image", "png");
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
   return add_compressor("image", "png", "png", FUN(compress, compressor_signature));
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
      .name = "compressor-png",
      .description = "Provides compression to png image format.",
      .version = VERSION,
      .requires = requires,
      .groups = groups,
   };

   return &info;
}
