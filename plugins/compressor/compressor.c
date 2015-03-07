#include <orbment/plugin.h>
#include <wlc/wlc.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include "config.h"

static const char *struct_signature = "c[],c[],*|1";
struct compressor {
   const char *name;
   const char *ext;
   void *function;
};

enum type {
   IMAGE,
   LAST
};

static const char *signatures[LAST] = {
   "u8[](p,u8[],sz*)|1", // IMAGE
};

static struct chck_iter_pool compressors[LAST];

enum type
type_for_string(const char *type)
{
   struct {
      const char *name;
      enum type type;
   } map[] = {
      { "image", IMAGE },
      { NULL, LAST },
   };

   for (uint32_t i = 0; map[i].name; ++i) {
      if (chck_cstreq(type, map[i].name))
         return map[i].type;
   }

   return LAST;
}

static bool
compressor_exists(struct chck_iter_pool *pool, const char *name)
{
   const struct compressor *c;
   chck_iter_pool_for_each(pool, c)
      if (chck_cstreq(name, c->name))
         return true;
   return false;
}

static bool
add_compressor(const char *type, const char *name, const char *ext, const struct function *fun)
{
   if (!name || !fun)
      return false;

   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      wlc_log(WLC_LOG_WARN, "Invalid type provided for '%s compressor'. (%s)", name, type);
      return false;
   }

   if (!chck_cstreq(fun->signature, signatures[t])) {
      wlc_log(WLC_LOG_WARN, "Wrong signature provided for '%s compressor' function. (%s != %s)", name, signatures[t], fun->signature);
      return false;
   }

   if (chck_cstr_starts_with(ext, ".")) {
      wlc_log(WLC_LOG_WARN, "Invalid extension for '%s compressor'. (%s) (remove leading dot)", name, ext);
      return false;
   }

   if (compressor_exists(&compressors[t], name)) {
      wlc_log(WLC_LOG_WARN, "Compressor with name '%s' already exists.", name);
   }

   struct compressor compressor = {
      .name = name,
      .ext = ext,
      .function = fun->function,
   };

   return chck_iter_pool_push_back(&compressors[t], &compressor);
}

static void
remove_compressor(const char *type, const char *name)
{
   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      wlc_log(WLC_LOG_WARN, "Tried to remove '%s compressor' with invalid type. (%s)", name, type);
      return;
   }

   struct compressor *c;
   chck_iter_pool_for_each(&compressors[t], c) {
      if (!chck_cstreq(c->name, name))
         continue;

      chck_iter_pool_remove(&compressors[t], _I - 1);
      break;
   }
}

static struct compressor*
list_compressors(const char *type, const char *stsign, const char *funsign, size_t *out_memb)
{
   if (out_memb)
      *out_memb = 0;

   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      wlc_log(WLC_LOG_WARN, "Tried to list compressors with invalid type. (%s)", type);
      return NULL;
   }

   if (!chck_cstreq(stsign, struct_signature)) {
      wlc_log(WLC_LOG_WARN, "Wrong struct signature. (%s != %s)", struct_signature, stsign);
      return NULL;
   }

   if (!chck_cstreq(funsign, signatures[t])) {
      wlc_log(WLC_LOG_WARN, "Wrong function signature. (%s != %s)", signatures[t], funsign);
      return NULL;
   }

   if (out_memb)
      *out_memb = compressors[t].items.count;

   return compressors[t].items.buffer;
}

bool
plugin_deinit(void)
{
   for (uint32_t i = 0; i < LAST; ++i)
      chck_iter_pool_release(&compressors[i]);
   return true;
}

bool
plugin_init(void)
{
   for (uint32_t i = 0; i < LAST; ++i) {
      if (!chck_iter_pool(&compressors[i], 1, 0, sizeof(struct compressor)))
         return false;
   }

   return true;
}

const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_compressor, "b(c[],c[],c[],fun)|1"),
      REGISTER_METHOD(remove_compressor, "v(c[],c[])|1"),
      REGISTER_METHOD(list_compressors, "*(c[],c[],c[],sz*)|1"),
      {0},
   };

   static const char *groups[] = {
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "compressor",
      .description = "Provides compression api.",
      .version = VERSION,
      .methods = methods,
      .groups = groups,
   };

   return &info;
}
