add_library(orbment-plugin-core-layouts SHARED core-layouts.c)
set_target_properties(orbment-plugin-core-layouts PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-layouts DESTINATION ${ORBMENT_PLUGINS_PATH})
