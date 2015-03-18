add_library(orbment-plugin-functionality SHARED core-functionality.c)
set_target_properties(orbment-plugin-functionality PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-functionality DESTINATION ${ORBMENT_PLUGINS_PATH})
