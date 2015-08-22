add_library(orbment-plugin-core-functionality SHARED core-functionality.c)
target_link_libraries(orbment-plugin-core-functionality ${ORBMENT_LIBRARIES})
set_target_properties(orbment-plugin-core-functionality PROPERTIES PREFIX "")
install(TARGETS orbment-plugin-core-functionality DESTINATION ${ORBMENT_PLUGINS_PATH})
