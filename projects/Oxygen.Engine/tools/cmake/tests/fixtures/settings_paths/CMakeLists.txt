cmake_minimum_required(VERSION 4.2)
project(SettingsPaths LANGUAGES CXX)
find_package(Oxygen CONFIG REQUIRED COMPONENTS Base)
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(
  settings-paths
  main.cpp
  "${OXYGEN_SOURCE}/Examples/DemoShell/Services/SettingsService.cpp"
)
target_compile_features(settings-paths PRIVATE cxx_std_23)
target_include_directories(settings-paths PRIVATE "${OXYGEN_SOURCE}/Examples")
target_link_libraries(
  settings-paths
  PRIVATE
    oxygen::base
    nlohmann_json::nlohmann_json
)
file(GLOB sdk_dlls "${OXYGEN_RUNTIME_DIR}/*.dll")
add_custom_command(
  TARGET settings-paths
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different ${sdk_dlls}
    "$<TARGET_FILE_DIR:settings-paths>"
  COMMAND_EXPAND_LISTS
  VERBATIM
)
