# CMake Windows helper functions module (adapted from obs-plugintemplate)
#
# Changes vs. upstream template:
#  * Uses the OBS 33 third-party plugin layout:
#      <plugins>/<name>/<name>.dll  (+ .pdb)
#      <plugins>/<name>/data/...    (manifest.json, locale/)
#    instead of the legacy <name>/bin/64bit layout.
#  * Works for targets defined in sub-directories (test plugins).
#  * The "rundir" mirrors the installed plugin folder so it can be copied as-is.

include_guard(GLOBAL)

include(helpers_common)

# set_target_properties_plugin: Set target properties for use in obs-studio
#   NO_INSTALL : do not add install() rules (used for test-only plugins)
function(set_target_properties_plugin target)
  set(options NO_INSTALL)
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 1 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  string(TIMESTAMP CURRENT_YEAR "%Y")
  set_target_properties(${target} PROPERTIES VERSION 0 SOVERSION ${PLUGIN_VERSION})

  if(NOT _STPO_NO_INSTALL)
    install(TARGETS ${target} RUNTIME DESTINATION "${target}" LIBRARY DESTINATION "${target}")
    install(
      FILES "$<TARGET_PDB_FILE:${target}>"
      CONFIGURATIONS RelWithDebInfo Debug Release
      DESTINATION "${target}"
      OPTIONAL
    )
  endif()

  set(_rundir "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/${target}")
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_rundir}"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>"
      "$<$<CONFIG:Debug,RelWithDebInfo,Release>:$<TARGET_PDB_FILE:${target}>>" "${_rundir}"
    COMMENT "Copy ${target} to rundir"
    VERBATIM
  )

  target_install_resources(${target} ${_STPO_NO_INSTALL})

  set(PROJECT_NAME ${target})
  configure_file("${CMAKE_SOURCE_DIR}/cmake/windows/resources/resource.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
  target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
endfunction()

# Helper function to add resources into the plugin folder
function(target_install_resources target no_install)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    file(GLOB_RECURSE data_files "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    foreach(data_file IN LISTS data_files)
      cmake_path(RELATIVE_PATH data_file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" OUTPUT_VARIABLE relative_path)
      cmake_path(GET relative_path PARENT_PATH relative_path)
      target_sources(${target} PRIVATE "${data_file}")
      source_group("Resources/${relative_path}" FILES "${data_file}")
    endforeach()

    if(NOT no_install)
      install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "${target}/data" USE_SOURCE_PERMISSIONS)
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
