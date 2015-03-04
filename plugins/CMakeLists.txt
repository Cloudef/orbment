SET(plugins
   test
   core-layouts
)

SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

INCLUDE_DIRECTORIES(${loliwm_SOURCE_DIR}/include ${WLC_INCLUDE_DIRS} ${loliwm_SOURCE_DIR}/lib/chck/src)
FOREACH (plugin ${plugins})
   ADD_SUBDIRECTORY(${plugin})
endforeach (plugin)
