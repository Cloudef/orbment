#ifndef __orbment_hooks_h__
#define __orbment_hooks_h__

#include <stdbool.h>

struct wlc_interface;

const struct wlc_interface* hooks_get_interface(void);
bool hooks_setup(void);

#endif /* __orbment_hooks_h__ */
