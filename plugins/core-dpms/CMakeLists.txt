add_library(orbment-plugin-core-dpms MODULE core-dpms.c)
target_link_libraries(orbment-plugin-core-dpms PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-core-dpms)
