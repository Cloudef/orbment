add_library(orbment-plugin-configuration MODULE configuration.c)
target_link_libraries(orbment-plugin-configuration PRIVATE ${ORBMENT_LIBRARIES})
add_plugins(orbment-plugin-configuration)

add_definitions(${INIHCK_DEFINITIONS})
include_directories(${INIHCK_INCLUDE_DIRS})
add_library(orbment-plugin-configuration-ini MODULE configuration-ini.c)
target_link_libraries(orbment-plugin-configuration-ini PRIVATE ${ORBMENT_LIBRARIES} ${INIHCK_LIBRARIES})
add_plugins(orbment-plugin-configuration-ini)
