#include <orbment/plugin.h>
#include <chck/string/string.h>
#include <chck/pool/pool.h>
#include "config.h"

static bool (*add_hook)(plugin_h, const char *name, const struct function*);

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

static struct {
   struct chck_iter_pool compressors[LAST];
   struct chck_iter_pool owners[LAST];
   plugin_h self;
} plugin;

static enum type
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
add_compressor(plugin_h caller, const char *type, const char *name, const char *ext, const struct function *fun)
{
   if (!name || !fun || !caller)
      return false;

   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      plog(plugin.self, PLOG_WARN, "Invalid type provided for '%s compressor'. (%s)", name, type);
      return false;
   }

   if (!chck_cstreq(fun->signature, signatures[t])) {
      plog(plugin.self, PLOG_WARN, "Wrong signature provided for '%s compressor' function. (%s != %s)", name, signatures[t], fun->signature);
      return false;
   }

   if (chck_cstr_starts_with(ext, ".")) {
      plog(plugin.self, PLOG_WARN, "Invalid extension for '%s compressor'. (%s) (remove leading dot)", name, ext);
      return false;
   }

   if (compressor_exists(&plugin.compressors[t], name)) {
      plog(plugin.self, PLOG_WARN, "Compressor with name '%s' already exists.", name);
   }

   struct compressor compressor = {
      .name = name,
      .ext = ext,
      .function = fun->function,
   };

   if (!chck_iter_pool_push_back(&plugin.compressors[t], &compressor))
      return false;

   if (!chck_iter_pool_push_back(&plugin.owners[t], &caller))
      goto error0;

   return true;

error0:
   chck_iter_pool_remove(&plugin.compressors[t], plugin.compressors[t].items.count - 1);
   return false;
}

static void
remove_compressor(plugin_h caller, const char *type, const char *name)
{
   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      plog(plugin.self, PLOG_WARN, "Tried to remove '%s compressor' with invalid type. (%s)", name, type);
      return;
   }

   struct compressor *c;
   chck_iter_pool_for_each(&plugin.compressors[t], c) {
      plugin_h *owner = chck_iter_pool_get(&plugin.owners[t], _I - 1);
      if (caller != *owner || !chck_cstreq(c->name, name))
         continue;

      chck_iter_pool_remove(&plugin.compressors[t], _I - 1);
      chck_iter_pool_remove(&plugin.owners[t], _I - 1);
      break;
   }
}

static void
remove_compressors_for_plugin(plugin_h caller)
{
   for (size_t t = 0; t < LAST; ++t) {
      plugin_h *owner;
      chck_iter_pool_for_each(&plugin.owners[t], owner) {
         if (caller != *owner)
            continue;

         chck_iter_pool_remove(&plugin.compressors[t], _I - 1);
         chck_iter_pool_remove(&plugin.owners[t], _I - 1);
         --_I;
      }
   }
}

static struct compressor*
list_compressors(const char *type, const char *stsign, const char *funsign, size_t *out_memb)
{
   if (out_memb)
      *out_memb = 0;

   enum type t;
   if ((t = type_for_string(type)) == LAST) {
      plog(plugin.self, PLOG_WARN, "Tried to list compressors with invalid type. (%s)", type);
      return NULL;
   }

   static const char *signature = "c[],c[],*|1";
   if (!chck_cstreq(stsign, signature)) {
      plog(plugin.self, PLOG_WARN, "Wrong struct signature. (%s != %s)", signature, stsign);
      return NULL;
   }

   if (!chck_cstreq(funsign, signatures[t])) {
      plog(plugin.self, PLOG_WARN, "Wrong function signature. (%s != %s)", signatures[t], funsign);
      return NULL;
   }

   return chck_iter_pool_to_c_array(&plugin.compressors[t], out_memb);
}

static void
plugin_deloaded(plugin_h ph)
{
   remove_compressors_for_plugin(ph);
}

#pragma GCC diagnostic ignored "-Wmissing-prototypes"

void
plugin_deinit(plugin_h self)
{
   (void)self;

   for (uint32_t i = 0; i < LAST; ++i) {
      chck_iter_pool_release(&plugin.compressors[i]);
      chck_iter_pool_release(&plugin.owners[i]);
   }
}

bool
plugin_init(plugin_h self)
{
   plugin.self = self;

   plugin_h orbment;
   if (!(orbment = import_plugin(self, "orbment")))
      return false;

   for (uint32_t i = 0; i < LAST; ++i) {
      if (!chck_iter_pool(&plugin.compressors[i], 1, 0, sizeof(struct compressor)) ||
          !chck_iter_pool(&plugin.owners[i], 1, 0, sizeof(plugin_h)))
         return false;
   }

   if (!(add_hook = import_method(self, orbment, "add_hook", "b(h,c[],fun)|1")))
      return false;

   return (add_hook(self, "plugin.deloaded", FUN(plugin_deloaded, "v(h)|1")));
}

PCONST const struct plugin_info*
plugin_register(void)
{
   static const struct method methods[] = {
      REGISTER_METHOD(add_compressor, "b(h,c[],c[],c[],fun)|1"),
      REGISTER_METHOD(remove_compressor, "v(h,c[],c[])|1"),
      REGISTER_METHOD(list_compressors, "*(c[],c[],c[],sz*)|1"),
      {0},
   };

   static const char *groups[] = {
      "compressor",
      NULL,
   };

   static const struct plugin_info info = {
      .name = "compressor",
      .description = "Compression api.",
      .version = VERSION,
      .methods = methods,
      .groups = groups,
   };

   return &info;
}
