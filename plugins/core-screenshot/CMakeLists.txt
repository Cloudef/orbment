add_library(orbment-plugin-core-screenshot MODULE core-screenshot.c)
target_link_libraries(orbment-plugin-core-screenshot PRIVATE ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-core-screenshot)
