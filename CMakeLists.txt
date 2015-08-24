CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(orbment VERSION 0.0.1 LANGUAGES C)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# CPack
set(CPACK_GENERATOR "7Z")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular Wayland compositor")
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# Includes
include(GNUInstallDirs)
include(FeatureSummary)
include(CPack)
set(ORBMENT_PLUGINS_FULL_PATH "${CMAKE_INSTALL_FULL_LIBDIR}/orbment" CACHE STRING "Systemwide plugins path (absolute)" FORCE)
set(ORBMENT_PLUGINS_PATH "${CMAKE_INSTALL_LIBDIR}/orbment" CACHE STRING "Systemwide plugins path" FORCE)

# Compiler options
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   include(GCCCompatibleCompilerOptions)
   add_compiler_warnings(-Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wsuggest-attribute=pure -Wsuggest-attribute=const)
   create_custom_linker_flags(Upstream -O1 --sort-common --as-needed -z,relro -z,now)
   check_c_compiler_flag(-fstack-protector-strong ok)
   if (ok)
      create_custom_compiler_flags(Upstream -g -O2 -fstack-protector-strong --param ssp-buffer-size=4 -fstack-check -flto)
   else ()
      create_custom_compiler_flags(Upstream -g -O2 -fstack-protector-all --param ssp-buffer-size=4 -fstack-check -flto)
   endif ()
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
