add_library(orbment-plugin-crappy-borders MODULE crappy-borders.c)
target_link_libraries(orbment-plugin-crappy-borders PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-crappy-borders)
