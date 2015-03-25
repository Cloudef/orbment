set(sources
   orbment.c
   plugin.c
   )

configure_file(config.h.in config.h @ONLY)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${WAYLAND_SERVER_INCLUDE_DIR}
   ${orbment_SOURCE_DIR}/include
   ${orbment_SOURCE_DIR}/lib/chck/src
   ${CMAKE_CURRENT_BINARY_DIR}
   )

add_executable(orbment ${sources})

target_link_libraries(orbment
   chck_orbment
   ${WLC_LIBRARY}
   ${WLC_LIBRARIES}
   )

# Install rules
install(TARGETS orbment DESTINATION bin)
