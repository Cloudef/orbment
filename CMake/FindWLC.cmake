# - Find wlc
# Find the wlc libraries
#
#  This module defines the following variables:
#     WLC_FOUND        - true if WLC_INCLUDE_DIR & WLC_LIBRARY are found
#     WLC_LIBRARIES    - Set when WLC_LIBRARY is found
#     WLC_INCLUDE_DIRS - Set when WLC_INCLUDE_DIR is found
#
#     WLC_INCLUDE_DIR  - where to find wlc.h, etc.
#     WLC_LIBRARY      - the wlc library
#

find_path(WLC_INCLUDE_DIR NAMES wlc/wlc.h)
find_library(WLC_LIBRARY NAMES wlc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(wlc DEFAULT_MSG WLC_LIBRARY WLC_INCLUDE_DIR)

if (WLC_FOUND)
   set(WLC_LIBRARIES ${WLC_LIBRARY})
   set(WLC_INCLUDE_DIRS ${WLC_INCLUDE_DIR})
endif ()

mark_as_advanced(WLC_INCLUDE_DIR WLC_LIBRARY)
