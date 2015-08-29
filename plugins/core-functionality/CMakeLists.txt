add_library(orbment-plugin-core-functionality MODULE core-functionality.c)
target_link_libraries(orbment-plugin-core-functionality PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-core-functionality)
