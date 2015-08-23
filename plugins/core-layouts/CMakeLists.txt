add_library(orbment-plugin-core-layouts MODULE core-layouts.c)
target_link_libraries(orbment-plugin-core-layouts PRIVATE ${ORBMENT_LIBRARIES})
set_target_properties(orbment-plugin-core-layouts PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-layouts DESTINATION ${ORBMENT_PLUGINS_PATH})
