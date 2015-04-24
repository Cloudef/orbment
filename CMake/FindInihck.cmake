# - Find inihck
# Find the inihck libraries
#
#  This module defines the following variables:
#     INIHCK_FOUND        - True if inihck is found
#     INIHCK_LIBRARIES    - inihck libraries
#     INIHCK_INCLUDE_DIRS - inihck include directories
#     INIHCK_DEFINITIONS  - Compiler switches required for using inihck
#

find_package(PkgConfig)
pkg_check_modules(PC_INIHCK QUIET inihck)
find_path(INIHCK_INCLUDE_DIRS NAMES inihck/inihck.h HINTS ${PC_INIHCK_INCLUDE_DIRS})
find_library(INIHCK_LIBRARIES NAMES inihck HINTS ${PC_INIHCK_LIBRARY_DIRS})

set(INIHCK_DEFINITIONS ${PC_INIHCK_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(inihck DEFAULT_MSG INIHCK_LIBRARIES INIHCK_INCLUDE_DIRS)
mark_as_advanced(INIHCK_LIBRARIES INIHCK_INCLUDE_DIRS)
