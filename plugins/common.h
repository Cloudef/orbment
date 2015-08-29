#ifndef __orbment_common_h__
#define __orbment_common_h__

#include <wlc/wlc.h>

// XXX: hack
enum {
   BIT_BEMENU = 1<<5,
};

enum direction {
   NEXT,
   PREV,
};

static inline bool
is_or(wlc_handle view)
{
   const uint32_t type = wlc_view_get_type(view);
   return (type & WLC_BIT_OVERRIDE_REDIRECT) || (type & BIT_BEMENU);
}

static inline bool
is_popup(wlc_handle view)
{
   const uint32_t type = wlc_view_get_type(view);
   return (type & WLC_BIT_POPUP);
}

static inline bool
is_managed(wlc_handle view)
{
   const uint32_t type = wlc_view_get_type(view);
   return !(type & WLC_BIT_UNMANAGED) && !(type & WLC_BIT_SPLASH);
}

static inline bool
is_modal(wlc_handle view)
{
   const uint32_t type = wlc_view_get_type(view);
   return (type & WLC_BIT_MODAL);
}

static inline bool
is_tiled(wlc_handle view)
{
   const uint32_t state = wlc_view_get_state(view);
   return !(state & WLC_BIT_FULLSCREEN) && !wlc_view_get_parent(view) && is_managed(view) && !is_or(view) && !is_modal(view) && !is_popup(view);
}

#endif /* __orbment_common_h__ */
