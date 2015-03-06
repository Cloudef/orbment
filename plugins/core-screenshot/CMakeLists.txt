add_library(orbment-plugin-core-screenshot SHARED core-screenshot.c)
set_target_properties(orbment-plugin-core-screenshot PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-screenshot DESTINATION ${ORBMENT_PLUGINS_PATH})
