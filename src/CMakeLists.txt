find_package(Math REQUIRED)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${CHCK_INCLUDE_DIRS}
   ${PROJECT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_BINARY_DIR}
   )

set(sources
   log.c
   plugin.c
   hooks.c
   signals.c
   orbment.c
   )

configure_file(config.h.in config.h @ONLY)

add_executable(orbment ${sources})
target_link_libraries(orbment PRIVATE ${CHCK_LIBRARIES} ${WLC_LIBRARIES} ${MATH_LIBRARY} ${CMAKE_DL_LIBS})

# Install rules
install(TARGETS orbment DESTINATION "${CMAKE_INSTALL_BINDIR}")

set(ORBMENT_LIBRARIES "" CACHE STRING "Libraries for linking Orbment plugins")
set(ORBMENT_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include" CACHE STRING "Include directories of Orbment")
