set(plugins
   core-layouts
)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${loliwm_SOURCE_DIR}/include
   ${loliwm_SOURCE_DIR}/lib/chck/src
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
foreach (plugin ${plugins})
   add_subdirectory(${plugin})
endforeach (plugin)
