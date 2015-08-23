#.rst:
# FindInihck
# -------
#
# Find inihck library
#
# Try to find wlc library. The following values are defined
#
# ::
#
#   INIHCK_FOUND         - True if inihck is available
#   INIHCK_INCLUDE_DIRS  - Include directories for inihck
#   INIHCK_LIBRARIES     - List of libraries for inihck
#   INIHCK_DEFINITIONS   - List of definitions for inihck
#
#=============================================================================
# Copyright (c) 2015 Jari Vetoniemi
#
# Distributed under the OSI-approved BSD License (the "License");
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

find_package(PkgConfig)
pkg_check_modules(PC_INIHCK QUIET inihck)
find_path(INIHCK_INCLUDE_DIRS NAMES inihck/inihck.h HINTS ${PC_INIHCK_INCLUDE_DIRS})
find_library(INIHCK_LIBRARIES NAMES inihck HINTS ${PC_INIHCK_LIBRARY_DIRS})

set(INIHCK_DEFINITIONS ${PC_INIHCK_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(inihck DEFAULT_MSG INIHCK_LIBRARIES INIHCK_INCLUDE_DIRS)
mark_as_advanced(INIHCK_LIBRARIES INIHCK_INCLUDE_DIRS INIHCK_DEFINITIONS)
