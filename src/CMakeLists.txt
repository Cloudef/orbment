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

if (UNIX)
   # chown to root and setid for group
   # NOTE: This is good default. However for most distributions
   # /dev/input is most likely owned by root, so instead chown root:input instead of root:root
   # to make sure process can only access input stuff before its permissions are dropped.
   #
   # If you have install that does not need permissions to access /dev/input,
   # you can forget about this step totally.
   install(CODE "
   execute_process(COMMAND chown root ${CMAKE_INSTALL_PREFIX}/bin/orbment)
   execute_process(COMMAND chmod g+s ${CMAKE_INSTALL_PREFIX}/bin/orbment)
   ")
endif ()
