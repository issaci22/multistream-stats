add_executable(msd-core-tests)
target_sources(
  msd-core-tests
  PRIVATE
    test-main.cpp
    test-helpers.hpp
    test-bitrate.cpp
    test-naming-format.cpp
    test-status-aggregate.cpp
    test-registry.cpp
    test-frame-window.cpp
    test-displayed-rows.cpp
    test-layout-math.cpp
)
target_include_directories(msd-core-tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/tests/third_party")
# doctest forward-declares std types unless told to include the real headers;
# MSVC 14.5x rejects that (C5285).
target_compile_definitions(msd-core-tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(msd-core-tests PRIVATE msd-core)
target_compile_options(msd-core-tests PRIVATE ${MSD_WARNING_FLAGS})

add_test(NAME msd-core-tests COMMAND msd-core-tests --reporters=console --no-intro=true)
