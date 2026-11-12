# CMake Linux helper functions module (adapted from obs-plugintemplate 3e7d7ac)
#
# Changes vs. upstream template:
#  * Same signature as the Windows helper: set_target_properties_plugin(<target> [NO_INSTALL] PROPERTIES ...).
#  * OBS 33 (non-legacy) layouts, as two install components:
#      system  <prefix>/<libdir>/obs-modules/plugins/<name>.so
#              <prefix>/share/obs/obs-modules/plugins/<name>/        (data: manifest.json, locale/)
#              -> used for the .deb (prefix /usr)
#      user    <name>/<name>.so + <name>/data/
#              -> extracted into $XDG_DATA_HOME/obs-studio/plugins/ (manual .tar.xz),
#                 or any folder named by OBS_PLUGINS_PATH
#  * The rundir (<build>/rundir/<config>/<name>/) mirrors the user layout, so it can be
#    used as OBS_PLUGINS_PATH directly.

include_guard(GLOBAL)

include(helpers_common)

# set_target_properties_plugin: Set target properties for use in obs-studio
function(set_target_properties_plugin target)
  set(options NO_INSTALL)
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 1 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  message(DEBUG "Setting additional properties for target ${target}...")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  set_target_properties(${target} PROPERTIES PREFIX "")

  if(NOT _STPO_NO_INSTALL)
    install(TARGETS ${target} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-modules/plugins" COMPONENT system)
    install(TARGETS ${target} LIBRARY DESTINATION "${target}" COMPONENT user)
  endif()

  if(TARGET plugin-support)
    target_link_libraries(${target} PRIVATE plugin-support)
  endif()

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMENT "Copy ${target} to rundir"
    VERBATIM
  )

  target_install_resources(${target} ${_STPO_NO_INSTALL})

  get_target_property(target_sources ${target} SOURCES)
  set(target_ui_files ${target_sources})
  list(FILTER target_ui_files INCLUDE REGEX ".+\\.(ui|qrc)")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "UI Files" FILES ${target_ui_files})
endfunction()

# Helper function to add the data/ folder (manifest.json, locale/) for both layouts
function(target_install_resources target no_install)
  message(DEBUG "Installing resources for target ${target}...")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    file(GLOB_RECURSE data_files "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    foreach(data_file IN LISTS data_files)
      cmake_path(
        RELATIVE_PATH
        data_file
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        OUTPUT_VARIABLE relative_path
      )
      cmake_path(GET relative_path PARENT_PATH relative_path)
      target_sources(${target} PRIVATE "${data_file}")
      source_group("Resources/${relative_path}" FILES "${data_file}")
    endforeach()

    if(NOT no_install)
      install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-modules/plugins/${target}"
        USE_SOURCE_PERMISSIONS
        COMPONENT system
      )
      install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        DESTINATION "${target}/data"
        USE_SOURCE_PERMISSIONS
        COMPONENT user
      )
    endif()

    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/${target}/data"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/${target}/data"
      COMMENT "Copy ${target} resources to rundir"
      VERBATIM
    )
  endif()
endfunction()
