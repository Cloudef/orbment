# - Find inihck
# Find the inihck libraries
#
#  This module defines the following variables:
#     INIHCK_FOUND        - true if INIHCK_INCLUDE_DIR & INIHCK_LIBRARY are found
#     INIHCK_LIBRARIES    - Set when INIHCK_LIBRARY is found
#     INIHCK_INCLUDE_DIRS - Set when INIHCK_INCLUDE_DIR is found
#
#     INIHCK_INCLUDE_DIR  - where to find inihck.h, etc.
#     INIHCK_LIBRARY      - the inihck library
#

find_path(INIHCK_INCLUDE_DIR NAMES inihck/inihck.h)
find_library(INIHCK_LIBRARY NAMES inihck)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(inihck DEFAULT_MSG INIHCK_LIBRARY INIHCK_INCLUDE_DIR)

if (INIHCK_FOUND)
   set(INIHCK_LIBRARIES ${INIHCK_LIBRARY})
   set(INIHCK_INCLUDE_DIRS ${INIHCK_INCLUDE_DIR})
endif ()

mark_as_advanced(INIHCK_INCLUDE_DIR INIHCK_LIBRARY)
