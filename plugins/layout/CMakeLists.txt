add_library(orbment-plugin-layout SHARED layout.c)
set_target_properties(orbment-plugin-layout PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-layout DESTINATION ${ORBMENT_PLUGINS_PATH})
