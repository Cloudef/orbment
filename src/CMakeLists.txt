set(sources
   log.c
   plugin.c
   hooks.c
   signals.c
   orbment.c
   )

configure_file(config.h.in config.h @ONLY)

find_library(MATH_LIBRARY m)
mark_as_advanced(MATH_LIBRARY)
if (NOT MATH_LIBRARY)
   message(FATAL_ERROR "libm is needed")
endif ()

if (UNIX)
   find_library(DL_LIBRARY dl)
   mark_as_advanced(DL_LIBRARY)
   if (NOT DL_LIBRARY)
      message(FATAL_ERROR "libdl is needed on unix systems")
   endif ()
endif (UNIX)

add_definitions(
   ${WLC_DEFINITIONS}
   )
include_directories(
   ${WLC_INCLUDE_DIRS}
   ${orbment_SOURCE_DIR}/include
   ${orbment_SOURCE_DIR}/lib/chck/src
   ${CMAKE_CURRENT_BINARY_DIR}
   )

add_executable(orbment ${sources})
target_link_libraries(orbment chck_orbment ${WLC_LIBRARIES} ${DL_LIBRARY} ${MATH_LIBRARY})

# Install rules
install(TARGETS orbment DESTINATION bin)

set(ORBMENT_LIBRARIES "" CACHE STRING "Libraries for linking Orbment plugins")
set(ORBMENT_INCLUDE_DIRS "${orbment_SOURCE_DIR}/include" CACHE STRING "Include directories of Orbment")
