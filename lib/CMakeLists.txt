add_compile_options(-fvisibility=hidden)

set(CHCK_BUILD_STATIC ON CACHE STRING "Build chck statically if not found systemwide" FORCE)
set(CHCK_BUILD_TESTS OFF CACHE STRING "Do not build chck tests" FORCE)
add_subproject(chck Chck)

# plugins will need wlc to be dynamic
set(WLC_BUILD_STATIC OFF CACHE STRING "Build wlc statically if not found systemwide" FORCE)
set(WLC_BUILD_EXAMPLES OFF CACHE STRING "Do not build wlc examples" FORCE)
set(WLC_BUILD_TESTS OFF CACHE STRING "Do not build wlc tests" FORCE)
add_subproject(wlc Wlc)

set(INIHCK_BUILD_STATIC ON CACHE STRING "Build inihck statically if not found systemwide" FORCE)
set(INIHCK_BUILD_TESTS OFF CACHE STRING "Do not build inihck tests" FORCE)
add_subproject(inihck)
