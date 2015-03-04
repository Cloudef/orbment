set(sources
   loliwm.c
   plugin.c
   )

configure_file(config.h.in config.h @ONLY)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${WAYLAND_SERVER_INCLUDE_DIR}
   ${loliwm_SOURCE_DIR}/include
   ${loliwm_SOURCE_DIR}/lib/chck/src
   ${CMAKE_CURRENT_BINARY_DIR}
   )

add_executable(loliwm ${sources})

target_link_libraries(loliwm
   chck_loliwm
   ${WLC_LIBRARY}
   ${WLC_LIBRARIES}
   )

# Install rules
install(TARGETS loliwm DESTINATION bin)

if (UNIX)
   # chown to root and setid for group
   # NOTE: This is good default. However for most distributions
   # /dev/input is most likely owned by root, so instead chown root:input instead of root:root
   # to make sure process can only access input stuff before its permissions are dropped.
   #
   # If you have install that does not need permissions to access /dev/input,
   # you can forget about this step totally.
   install(CODE "
   execute_process(COMMAND chown root ${CMAKE_INSTALL_PREFIX}/bin/loliwm)
   execute_process(COMMAND chmod g+s ${CMAKE_INSTALL_PREFIX}/bin/loliwm)
   ")
endif ()
