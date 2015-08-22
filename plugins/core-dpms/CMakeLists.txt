add_library(orbment-plugin-core-dpms SHARED core-dpms.c)
target_link_libraries(orbment-plugin-core-dpms ${ORBMENT_LIBRARIES} chck_orbment)
set_target_properties(orbment-plugin-core-dpms PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-dpms DESTINATION ${ORBMENT_PLUGINS_PATH})
