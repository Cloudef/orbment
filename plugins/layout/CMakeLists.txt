add_library(orbment-plugin-layout MODULE layout.c)
target_link_libraries(orbment-plugin-layout PRIVATE ${ORBMENT_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-layout)
