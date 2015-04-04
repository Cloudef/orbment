add_subproject(wlc WLC)

set(INIHCK_BUILD_STATIC ON CACHE STRING "Build inihck statically if not found systemwide" FORCE)
set(INIHCK_BUILD_TESTS OFF CACHE STRING "Do not build inihck tests" FORCE)
add_subproject(inihck)

add_subdirectory(chck)
