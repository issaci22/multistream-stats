# Test-only OBS module: simulated streaming outputs + scripted test harness.
# Built only with -DMSD_BUILD_MOCK_PLUGIN=ON (presets windows-x64-dev, windows-ci-x64,
# macos-ci, ubuntu-ci-x86_64) and never installed or packaged.
add_library(msd-mock-outputs MODULE)
target_sources(msd-mock-outputs PRIVATE mock-plugin.cpp mock-output.cpp mock-output.hpp harness.cpp harness.hpp)
target_include_directories(msd-mock-outputs PRIVATE "${CMAKE_SOURCE_DIR}/src/third_party")
target_link_libraries(msd-mock-outputs PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets)
target_compile_options(msd-mock-outputs PRIVATE ${MSD_WARNING_FLAGS})
set_target_properties(msd-mock-outputs PROPERTIES AUTOMOC ON)
set_target_properties_plugin(msd-mock-outputs NO_INSTALL PROPERTIES OUTPUT_NAME msd-mock-outputs)
