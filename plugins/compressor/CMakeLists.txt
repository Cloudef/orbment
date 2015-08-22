add_library(orbment-plugin-compressor SHARED compressor.c)
target_link_libraries(orbment-plugin-compressor ${ORBMENT_LIBRARIES})
set_target_properties(orbment-plugin-compressor PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-compressor DESTINATION ${ORBMENT_PLUGINS_PATH})

set(compressors ppm)
set(ppm_src ${orbment_SOURCE_DIR}/lib/chck/src/chck/buffer/buffer.c)

find_package(PNG)
if (PNG_FOUND)
   list(APPEND compressors png)
   set(png_src ${orbment_SOURCE_DIR}/lib/chck/src/chck/buffer/buffer.c)
   set(png_lib ${PNG_LIBRARIES})
   set(png_inc ${PNG_INCLUDES})
endif ()

foreach (c ${compressors})
   include_directories(${${c}_inc})
   add_library(orbment-plugin-compressor-${c} SHARED compressor-${c}.c ${${c}_src})
   target_link_libraries(orbment-plugin-compressor-${c} ${${c}_lib} ${ORBMENT_LIBRARIES})
   set_target_properties(orbment-plugin-compressor-${c} PROPERTIES PREFIX "")
   install(TARGETS orbment-plugin-compressor-${c} DESTINATION ${ORBMENT_PLUGINS_PATH})
endforeach ()

