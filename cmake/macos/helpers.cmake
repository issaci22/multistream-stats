# CMake macOS helper functions module (adapted from obs-plugintemplate 3e7d7ac)
#
# Changes vs. upstream template:
#  * Same signature as the Windows helper: set_target_properties_plugin(<target> [NO_INSTALL] PROPERTIES ...).
#    NO_INSTALL is used for test-only plugins, which are never installed or packaged.
#  * OBS 33 macOS layout: <name>.plugin/Contents/MacOS/<name> + Contents/Resources/ (= the module's data dir).
#  * Every plugin built here gets its own bundle identifier; only the main plugin uses MACOS_BUNDLEID as is.
#  * The rundir (<build>/rundir/<config>/) holds every plugin bundle, so it can be used as OBS_PLUGINS_PATH.
#  * No installer package at install time: .github/scripts/package-macos.sh builds the PKG, DMG and ZIP,
#    and takes the dSYM straight from the build tree.

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

  if(target STREQUAL CMAKE_PROJECT_NAME)
    set(_bundle_id "${MACOS_BUNDLEID}")
  else()
    set(_bundle_id "${MACOS_BUNDLEID}.${target}")
  endif()

  string(TIMESTAMP CURRENT_YEAR "%Y")
  set_target_properties(
    ${target}
    PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      XCODE_ATTRIBUTE_PRODUCT_NAME ${target}
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${_bundle_id}
      XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION ${PLUGIN_BUILD_NUMBER}
      XCODE_ATTRIBUTE_MARKETING_VERSION ${PLUGIN_VERSION}
      XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
      XCODE_ATTRIBUTE_INFOPLIST_FILE ""
      XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName ${target}
      XCODE_ATTRIBUTE_INFOPLIST_KEY_NSHumanReadableCopyright "(c) ${CURRENT_YEAR} ${PLUGIN_AUTHOR}"
      XCODE_ATTRIBUTE_INSTALL_PATH "$(USER_LIBRARY_DIR)/Application Support/obs-studio/plugins"
  )

  if(TARGET plugin-support)
    target_link_libraries(${target} PRIVATE plugin-support)
  endif()

  target_install_resources(${target})

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/$<TARGET_BUNDLE_DIR_NAME:${target}>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:${target}>"
      "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/$<TARGET_BUNDLE_DIR_NAME:${target}>"
    COMMENT "Copy ${target} to rundir"
    VERBATIM
  )

  get_target_property(target_sources ${target} SOURCES)
  set(target_ui_files ${target_sources})
  list(FILTER target_ui_files INCLUDE REGEX ".+\\.(ui|qrc)")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "UI Files" FILES ${target_ui_files})

  if(NOT _STPO_NO_INSTALL)
    install(TARGETS ${target} LIBRARY DESTINATION . COMPONENT plugin)
  endif()
endfunction()

# target_install_resources: Helper function to add resources into bundle
function(target_install_resources target)
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
      set_property(SOURCE "${data_file}" PROPERTY MACOSX_PACKAGE_LOCATION "Resources/${relative_path}")
      source_group("Resources/${relative_path}" FILES "${data_file}")
    endforeach()
  endif()
endfunction()
