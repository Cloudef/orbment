#ifndef __plugin_h__
#define __plugin_h__

#include <loliwm/plugin.h>
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

#endif
