add_library(orbment-plugin-compressor MODULE compressor.c)
target_link_libraries(orbment-plugin-compressor PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-compressor)

set(compressors ppm)
set(ppm_lib ${CHCK_LIBRARIES})

find_package(PNG)
if (PNG_FOUND)
   list(APPEND compressors png)
   set(png_lib ${PNG_LIBRARIES} ${CHCK_LIBRARIES})
   set(png_inc ${PNG_INCLUDES})
endif ()

foreach (c ${compressors})
   include_directories(${${c}_inc})
   add_library(orbment-plugin-compressor-${c} MODULE compressor-${c}.c)
   target_link_libraries(orbment-plugin-compressor-${c} PRIVATE ${${c}_lib} ${ORBMENT_LIBRARIES})
   add_plugins(orbment-plugin-compressor-${c})
endforeach ()

