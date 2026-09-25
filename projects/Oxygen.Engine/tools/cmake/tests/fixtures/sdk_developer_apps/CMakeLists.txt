cmake_minimum_required(VERSION 4.2)
project(OxygenDeveloperApps LANGUAGES CXX)
find_package(
  Oxygen
  CONFIG
  REQUIRED
  COMPONENTS
    Scene
    OxCo
)

add_executable(scene-playground scene.cpp)
target_link_libraries(scene-playground PRIVATE oxygen::scene)
add_executable(async-loading async.cpp)
target_link_libraries(async-loading PRIVATE oxygen::oxco)

foreach(app IN ITEMS scene-playground async-loading)
  target_compile_features(${app} PRIVATE cxx_std_23)
  if(MSVC)
    target_compile_options(
      ${app}
      PRIVATE
        /W4
        /permissive-
        /Zc:preprocessor
    )
  endif()
  if(WIN32)
    file(GLOB sdk_dlls "${OXYGEN_RUNTIME_DIR}/*.dll")
    add_custom_command(
      TARGET ${app}
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${sdk_dlls}
        "$<TARGET_FILE_DIR:${app}>"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()
endforeach()
enable_testing()
add_test(
  NAME scene-hierarchy
  COMMAND
    scene-playground --verify
)
add_test(
  NAME asynchronous-loading
  COMMAND
    async-loading --verify
)
