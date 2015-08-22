add_library(orbment-plugin-layout SHARED layout.c)
target_link_libraries(orbment-plugin-layout ${ORBMENT_LIBRARIES})
set_target_properties(orbment-plugin-layout PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-layout DESTINATION ${ORBMENT_PLUGINS_PATH})
