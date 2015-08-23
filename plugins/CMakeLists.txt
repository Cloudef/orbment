set(plugins
   keybind
   vfs
   layout
   compressor
   core-input
   core-functionality
   core-layouts
   core-screenshot
   core-dpms
   configuration
   autostart
)

include_directories(
   ${WLC_INCLUDE_DIRS}
   ${CHCK_INCLUDE_DIRS}
   ${XKBCOMMON_INCLUDE_DIR}
   ${PROJECT_SOURCE_DIR}/include
   ${CMAKE_BINARY_DIR}/src # for config.h
   ${CMAKE_CURRENT_SOURCE_DIR} # for common.h
   )

macro(add_plugins)
   foreach (plugin ${ARGN})
      set_target_properties(${plugin} PROPERTIES PREFIX "")
   endforeach ()
   install(TARGETS ${ARGV} DESTINATION ${ORBMENT_PLUGINS_PATH})
endmacro()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
foreach (plugin ${plugins})
   add_subdirectory(${plugin})
endforeach (plugin)
