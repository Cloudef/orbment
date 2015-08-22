#ifndef __orbment_plugin_private_h__
#define __orbment_plugin_private_h__

#include <orbment/plugin.h>
#include <stdbool.h>
#include "chck/string/string.h"
#include "chck/pool/pool.h"

struct plugin {
   struct chck_iter_pool needed;
   struct chck_string path;
   struct plugin_info info;
   bool (*init)(plugin_h self);
   void (*deinit)(plugin_h self);
   plugin_h handle;
   void *dl;
   bool loaded;
};

void plugin_set_callbacks(void (*loaded)(const struct plugin*), void (*deloaded)(const struct plugin*));
void plugin_remove_all(void);
void plugin_load_all(void);
PNONULLV(1) bool plugin_register(struct plugin *plugin, const struct plugin_info* (*reg)(void));
PNONULL bool plugin_register_from_path(const char *path);

#endif /* __orbment_plugin_private_h__ */
