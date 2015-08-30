add_library(orbment-plugin-core-input MODULE core-input.c)
target_link_libraries(orbment-plugin-core-input PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-core-input)
