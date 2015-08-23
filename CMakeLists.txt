CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(orbment VERSION 0.0.1 LANGUAGES C)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# CPack
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular Wayland compositor")
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# Includes
include(GNUInstallDirs)
include(FeatureSummary)
include(CPack)
set(ORBMENT_PLUGINS_FULL_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/orbment" CACHE STRING "Systemwide plugins path (absolute)" FORCE)
set(ORBMENT_PLUGINS_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/orbment" CACHE STRING "Systemwide plugins path" FORCE)

# Warnings
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now)
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ldflags}")

   if ("${CMAKE_C_FLAGS}" STREQUAL "")
      add_definitions(-O2)
      add_definitions(-fstack-protector-strong -fstack-check -flto)
   endif ()

   add_definitions(
      -Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal
      -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wsuggest-attribute=pure -Wsuggest-attribute=const)
endif ()

# -std=c99 -fpic -fpie -D_DEFAULT_SOURCE
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_definitions(-D_DEFAULT_SOURCE)

add_subdirectory(src)
add_subdirectory(plugins)

configure_file(orbment.1.in man/man1/orbment.1 @ONLY)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/man/man1" DESTINATION "${CMAKE_INSTALL_MANDIR}")
install(DIRECTORY include/orbment DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if ("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
   feature_summary(WHAT ALL)
endif ()
