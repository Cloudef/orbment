set(plugins
   compressor
   core-layouts
   core-screenshot
)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${orbment_SOURCE_DIR}/include
   ${orbment_SOURCE_DIR}/lib/chck/src
   ${CMAKE_BINARY_DIR}/src # for config.h
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
foreach (plugin ${plugins})
   add_subdirectory(${plugin})
endforeach (plugin)
