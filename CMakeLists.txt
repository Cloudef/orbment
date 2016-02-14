CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(orbment VERSION 0.0.1 LANGUAGES C)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# Public depencies of wlc
find_package(XKBCommon REQUIRED)
add_definitions(${XKBCOMMON_DEFINTIONS})
include_directories(${XKBCOMMON_INCLUDE_DIRS})

find_package(LibInput REQUIRED)
add_definitions(${LIBINPUT_DEFINTIONS})
include_directories(${LIBINPUT_INCLUDE_DIRS})

# CPack
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
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
include(GCCCompatibleCompilerOptions)

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -O1 --sort-common --as-needed -z,relro -z,now)
   set(cflags -flto -fuse-linker-plugin)
endif ()

check_c_compiler_flag(-fstack-protector-strong has_fstack_protector_strong)
if (has_fstack_protector_strong)
   list(APPEND cflags -fstack-protector-strong -fstack-check --param ssp-buffer-size=4)
else ()
   list(APPEND cflags -fstack-protector-all -fstack-check --param ssp-buffer-size=4)
endif ()

create_custom_linker_flags(Upstream ${ldflags})
create_custom_compiler_flags(Upstream -g -O2 ${cflags})

add_compiler_warnings(-Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal -Wcast-align -Wpointer-arith -Wmissing-prototypes)

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   add_compiler_warnings(-Wsuggest-attribute=pure -Wsuggest-attribute=const)
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang")
   add_compiler_warnings(-Wno-pointer-bool-conversion -Wno-missing-field-initializers -Wno-missing-braces)
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
