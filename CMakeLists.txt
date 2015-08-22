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
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now)
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ldflags}")
   add_definitions(
      -Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal
      -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wsuggest-attribute=pure -Wsuggest-attribute=const)
   add_definitions(-fstack-protector-strong -fstack-check -pie -flto)
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
