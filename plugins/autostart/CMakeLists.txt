add_library(orbment-plugin-autostart SHARED autostart.c)
set_target_properties(orbment-plugin-autostart PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-autostart DESTINATION ${ORBMENT_PLUGINS_PATH})
