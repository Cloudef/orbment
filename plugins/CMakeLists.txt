set(plugins
   layout
   compressor
   core-functionality
   core-layouts
   core-screenshot
   configuration
)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${orbment_SOURCE_DIR}/include
   ${orbment_SOURCE_DIR}/lib/chck/src
   ${orbment_SOURCE_DIR}/src # for common.h
   ${CMAKE_BINARY_DIR}/src # for config.h
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
foreach (plugin ${plugins})
   add_subdirectory(${plugin})
endforeach (plugin)
