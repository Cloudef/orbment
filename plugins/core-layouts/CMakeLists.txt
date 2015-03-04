add_library(loliwm-plugin-core-layouts SHARED core-layouts.c)
set_target_properties(loliwm-plugin-core-layouts PROPERTIES PREFIX "")
install(TARGETS loliwm-plugin-core-layouts DESTINATION ${LOLIWM_PLUGINS_PATH})
