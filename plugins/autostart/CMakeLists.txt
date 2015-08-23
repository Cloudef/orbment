add_library(orbment-plugin-autostart MODULE autostart.c)
target_link_libraries(orbment-plugin-autostart PRIVATE ${ORBMENT_LIBRARIES})
add_plugins(orbment-plugin-autostart)
