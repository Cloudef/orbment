add_library(orbment-plugin-keybind MODULE keybind.c)
target_link_libraries(orbment-plugin-keybind PRIVATE ${ORBMENT_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${CHCK_LIBRARIES})
add_plugins(orbment-plugin-keybind)
