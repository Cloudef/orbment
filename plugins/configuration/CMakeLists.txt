add_library(orbment-plugin-configuration SHARED configuration.c)
set_target_properties(orbment-plugin-configuration PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-configuration DESTINATION ${ORBMENT_PLUGINS_PATH})

include_directories(${inihck_SOURCE_DIR}/src)
add_library(orbment-plugin-configuration-ini SHARED configuration-ini.c)
target_link_libraries(orbment-plugin-configuration-ini inihck)
set_target_properties(orbment-plugin-configuration-ini PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-configuration-ini DESTINATION ${ORBMENT_PLUGINS_PATH})
