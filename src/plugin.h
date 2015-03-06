#ifndef __orbment_plugin_private_h__
#define __orbment_plugin_private_h__

#include <orbment/plugin.h>
#include <stdbool.h>
#include "chck/string/string.h"

struct plugin {
   struct plugin_info info;
   struct chck_string path;
   bool (*init)(void);
   bool (*deinit)(void);
   void *handle;
};

bool register_plugin(struct plugin *plugin, const struct plugin_info* (*reg)(void));
bool register_plugin_from_path(const char *path);

#endif /* __orbment_plugin_private_h__ */
