add_library(orbment-plugin-core-screenshot SHARED core-screenshot.c)
target_link_libraries(orbment-plugin-core-screenshot chck_orbment)
set_target_properties(orbment-plugin-core-screenshot PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-screenshot DESTINATION ${ORBMENT_PLUGINS_PATH})
