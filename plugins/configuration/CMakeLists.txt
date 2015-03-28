add_library(orbment-plugin-configuration SHARED configuration.c)
set_target_properties(orbment-plugin-configuration PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-configuration DESTINATION ${ORBMENT_PLUGINS_PATH})

add_library(orbment-plugin-configuration-ini SHARED configuration-ini.c ciniparser/ciniparser.c ciniparser/dictionary.c)
set_target_properties(orbment-plugin-configuration-ini PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-configuration-ini DESTINATION ${ORBMENT_PLUGINS_PATH})
