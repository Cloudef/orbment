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
   bool (*init)(void);
   bool (*deinit)(void);
   void *handle;
   bool loaded;
};

void deload_plugins(void);
void load_plugins(void);
bool register_plugin(struct plugin *plugin, const struct plugin_info* (*reg)(void));
bool register_plugin_from_path(const char *path);

#endif /* __orbment_plugin_private_h__ */
