CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(orbment C)
set(ORBMENT_NAME "Orbment")
set(ORBMENT_DESCRIPTION "Modular Wayland Compositor")
set(ORBMENT_VERSION "0.0.1")
set(ORBMENT_PLUGINS_PATH "${CMAKE_INSTALL_PREFIX}/lib/orbment" CACHE STRING "Systemwide plugins path")
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

add_definitions(-std=c99 -D_DEFAULT_SOURCE)

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(plugins)

install(FILES orbment.1 DESTINATION man/man1)
install(DIRECTORY include/orbment DESTINATION include)
