add_library(orbment-plugin-core-dpms SHARED core-dpms.c)
set_target_properties(orbment-plugin-core-dpms PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-dpms DESTINATION ${ORBMENT_PLUGINS_PATH})
