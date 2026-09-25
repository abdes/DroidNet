cmake_minimum_required(VERSION 4.2)
project(HelloOxygen LANGUAGES CXX)

find_package(Oxygen CONFIG REQUIRED COMPONENTS Core)
add_executable(hello-oxygen main.cpp)
target_compile_features(hello-oxygen PRIVATE cxx_std_23)
target_link_libraries(hello-oxygen PRIVATE oxygen::core)

# This small sample stages the SDK DLLs beside its executable so it runs from an
# ordinary shell. A game's final packaging can select its required runtime set.
if(WIN32)
  file(GLOB sdk_dlls "${OXYGEN_RUNTIME_DIR}/*.dll")
  if(sdk_dlls)
    add_custom_command(
      TARGET hello-oxygen
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${sdk_dlls}
        "$<TARGET_FILE_DIR:hello-oxygen>"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()
endif()
