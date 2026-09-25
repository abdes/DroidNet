cmake_minimum_required(VERSION 4.2)
project(OxygenPackageConsumer LANGUAGES CXX)

find_package(Oxygen CONFIG REQUIRED)
add_executable(oxygen-consumer main.cpp)
target_compile_features(oxygen-consumer PRIVATE cxx_std_23)
target_link_libraries(oxygen-consumer PRIVATE ${OXYGEN_TEST_TARGETS})
foreach(
  module
  IN
  ITEMS
    composition
    oxco
    serio
    textwrap
    clap
    core
    scene
)
  if(TARGET oxygen::${module})
    string(TOUPPER "${module}" definition)
    target_compile_definitions(
      oxygen-consumer
      PRIVATE
        OXYGEN_TEST_${definition}=1
    )
  endif()
endforeach()
target_compile_definitions(
  oxygen-consumer
  PRIVATE
    OXYGEN_TEST_CHECKER=$<BOOL:${OXYGEN_TEST_CHECKER}>
)
