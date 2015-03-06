add_library(orbment-plugin-compressor SHARED compressor.c)
set_target_properties(orbment-plugin-compressor PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-compressor DESTINATION ${ORBMENT_PLUGINS_PATH})

set(compressors
   ppm)

set(ppm_src ${orbment_SOURCE_DIR}/lib/chck/src/chck/buffer/buffer.c)

foreach (c ${compressors})
   add_library(orbment-plugin-compressor-${c} SHARED compressor-${c}.c ${${c}_src})
   set_target_properties(orbment-plugin-compressor-${c} PROPERTIES PREFIX "")
   install(TARGETS orbment-plugin-compressor-${c} DESTINATION ${ORBMENT_PLUGINS_PATH})
endforeach ()

