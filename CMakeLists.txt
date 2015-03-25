CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(orbment C)
set(ORBMENT_NAME "Orbment")
set(ORBMENT_DESCRIPTION "Modular Wayland Compositor")
set(ORBMENT_VERSION "0.0.1")
set(ORBMENT_PLUGINS_PATH "${CMAKE_INSTALL_PREFIX}/lib/orbment" CACHE STRING "Systemwide plugins path" FORCE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${orbment_SOURCE_DIR}/CMake)

# Includes
include(subproject)

# Warnings
# FIXME: Use CMake module made for this instead.
if (MSVC)
   if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
      string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
   else ()
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
   endif ()
elseif (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wall -Wextra -Wno-variadic-macros -Wno-long-long")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-variadic-macros -Wno-long-long")
endif ()

if (UNIX AND CMAKE_COMPILER_IS_GNUCC)
   set(CMAKE_POSITION_INDEPENDENT_CODE ON)
   if (${CMAKE_VERSION} VERSION_LESS 2.8.9)
      add_definitions(-fPIC)
   endif ()
endif ()

add_definitions(-std=c99 -D_DEFAULT_SOURCE)

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(plugins)

configure_file(orbment.1.in man/man1/orbment.1 @ONLY)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/man DESTINATION share)
install(DIRECTORY include/orbment DESTINATION include)
