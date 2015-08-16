add_library(orbment-plugin-keybind SHARED keybind.c)
set_target_properties(orbment-plugin-keybind PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-keybind DESTINATION ${ORBMENT_PLUGINS_PATH})
