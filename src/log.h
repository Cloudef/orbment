#ifndef __orbment_log_h__
#define __orbment_log_h__

#include <stdio.h>

enum plugin_log_type;

void log_set_file(const char *path);
void log_open(void);
void log_close(void);
void log_backtrace(void);

/** this is exposed for plugin.c, use plog instead. */
void logv(enum plugin_log_type type, const char *prefix, const char *fmt, va_list ap);

#endif /* __orbment_log_h__ */
