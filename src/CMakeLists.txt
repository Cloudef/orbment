SET(SRC
   loliwm.c
   )

# We use wayland-util
FIND_PACKAGE(Wayland REQUIRED)

# Math lib
FIND_LIBRARY(MATH_LIBRARY m)
MARK_AS_ADVANCED(MATH_LIBRARY)
IF (NOT MATH_LIBRARY)
   MESSAGE(FATAL_ERROR "Math library was not found")
ENDIF ()

ADD_DEFINITIONS(-std=c99 -D_DEFAULT_SOURCE)
INCLUDE_DIRECTORIES(${WLC_INCLUDE_DIRS} ${WAYLAND_SERVER_INCLUDE_DIR})
ADD_EXECUTABLE(loliwm ${SRC})
TARGET_LINK_LIBRARIES(loliwm ${WLC_LIBRARY} ${WLC_LIBRARIES} ${WAYLAND_SERVER_LIBRARIES} ${MATH_LIBRARY})

# Install rules
INSTALL(TARGETS loliwm DESTINATION bin)

if (UNIX)
   # chown to root and setid for group
   # NOTE: This is good default. However for most distributions
   # /dev/input is most likely owned by root, so instead chown root:input instead of root:root
   # to make sure process can only access input stuff before its permissions are dropped.
   #
   # If you have install that does not need permissions to access /dev/input,
   # you can forget about this step totally.
   INSTALL(CODE "
   execute_process(COMMAND chown root ${CMAKE_INSTALL_PREFIX}/bin/loliwm)
   execute_process(COMMAND chmod g+s ${CMAKE_INSTALL_PREFIX}/bin/loliwm)
   ")
endif ()
