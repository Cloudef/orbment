set(sources
   orbment.c
   plugin.c
   )

configure_file(config.h.in config.h @ONLY)

find_package(XKBCommon REQUIRED)

if (UNIX)
   find_library(DL_LIBRARY dl)
   mark_as_advanced(DL_LIBRARY)
   if (NOT DL_LIBRARY)
      message(FATAL_ERROR "libdl is needed on unix systems")
   endif ()
endif (UNIX)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${WAYLAND_SERVER_INCLUDE_DIR}
   ${XKBCOMMON_INCLUDE_DIR}
   ${orbment_SOURCE_DIR}/include
   ${orbment_SOURCE_DIR}/lib/chck/src
   ${CMAKE_CURRENT_BINARY_DIR}
   )

add_executable(orbment ${sources})

target_link_libraries(orbment
   chck_orbment
   ${WLC_LIBRARY}
   ${WLC_LIBRARIES}
   ${XKBCOMMON_LIBRARIES}
   ${DL_LIBRARY}
   )

# Install rules
install(TARGETS orbment DESTINATION bin)
