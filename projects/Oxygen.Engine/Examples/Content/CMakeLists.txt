# The SDK ships a deliberately small, self-contained set of authored samples.
set(
  OXYGEN_SHOWCASE_SCENES
  sdk-lantern
  sdk-furniture
  sdk-materials
  city-environment-validation
  emissive
  physics_domains
  multi-script
  point-shadow-validation
  spot-shadow-validation
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(OXYGEN_SHOWCASE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/showcase")
set(OXYGEN_SHOWCASE_CONFIGS "${CMAKE_CONFIGURATION_TYPES}")
if(NOT OXYGEN_SHOWCASE_CONFIGS)
  set(OXYGEN_SHOWCASE_CONFIGS "${CMAKE_BUILD_TYPE}")
endif()
foreach(_config IN LISTS OXYGEN_SHOWCASE_CONFIGS)
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_showcase.py"
      --source "${CMAKE_CURRENT_SOURCE_DIR}" --output
      "${OXYGEN_SHOWCASE_ROOT}/${_config}/Content" --scenes
      ${OXYGEN_SHOWCASE_SCENES}
    COMMAND_ERROR_IS_FATAL ANY
  )
  file(
    STRINGS
    "${OXYGEN_SHOWCASE_ROOT}/${_config}/inputs.txt"
    OXYGEN_SHOWCASE_INPUTS
  )
endforeach()
set(OXYGEN_SHOWCASE_CONTENT "${OXYGEN_SHOWCASE_ROOT}/$<CONFIG>/Content")
set_property(
  DIRECTORY
  APPEND
  PROPERTY
    CMAKE_CONFIGURE_DEPENDS
      ${OXYGEN_SHOWCASE_INPUTS}
      "${CMAKE_CURRENT_SOURCE_DIR}/prepare_showcase.py"
)
include(RuntimeEnvironment)
oxygen_get_runtime_launcher(oxygen-cooker-importtool OXYGEN_SHOWCASE_LAUNCHER)
set(OXYGEN_SHOWCASE_STAMP "${OXYGEN_SHOWCASE_ROOT}/cooked-$<CONFIG>.stamp")
add_custom_command(
  OUTPUT
    "${OXYGEN_SHOWCASE_STAMP}"
  BYPRODUCTS
    "${OXYGEN_SHOWCASE_CONTENT}/.cooked/container.index.bin"
    "${OXYGEN_SHOWCASE_CONTENT}/pak/all.pak"
    "${OXYGEN_SHOWCASE_CONTENT}/pak/all.catalog.json"
    "${OXYGEN_SHOWCASE_CONTENT}/pak/all.manifest.json"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" powershell.exe -NoLogo -NoProfile
    -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/cook_scenes.ps1"
    -All -NoTUI -ContentRoot "${OXYGEN_SHOWCASE_CONTENT}" -ToolPath
    "$<TARGET_FILE:oxygen-cooker-importtool>"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" powershell.exe -NoLogo -NoProfile
    -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/pak_content.ps1"
    -CookedRoot "${OXYGEN_SHOWCASE_CONTENT}/.cooked" -OutputDir
    "${OXYGEN_SHOWCASE_CONTENT}/pak" -ToolPath
    "$<TARGET_FILE:oxygen-cooker-paktool>"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" "$<TARGET_FILE:oxygen-cooker-inspector>"
    validate "${OXYGEN_SHOWCASE_CONTENT}/.cooked"
  COMMAND
    "${CMAKE_COMMAND}" -E touch "${OXYGEN_SHOWCASE_STAMP}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prepare_showcase.py"
    ${OXYGEN_SHOWCASE_INPUTS}
    oxygen-cooker-importtool
    oxygen-cooker-paktool
    oxygen-cooker-inspector
  COMMAND_EXPAND_LISTS
  VERBATIM
  COMMENT "Preparing the installed Oxygen showcase content"
)
add_custom_target(
  oxygen-showcase-content
  ALL
  DEPENDS
    "${OXYGEN_SHOWCASE_STAMP}"
)
if(OXYGEN_INSTALL)
  install(
    CODE
      "
    if(NOT EXISTS \"${OXYGEN_SHOWCASE_CONTENT}/.cooked/container.index.bin\"
       OR NOT EXISTS \"${OXYGEN_SHOWCASE_CONTENT}/pak/all.pak\")
      message(FATAL_ERROR \"Build the selected configuration before installing the SDK: showcase content is missing.\")
    endif()
  "
    COMPONENT Oxygen_data
  )
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/showcase-assets.schema.json"
    DESTINATION "${OXYGEN_INSTALL_SCHEMAS}"
    COMPONENT Oxygen_data
  )
  install(
    DIRECTORY
      "${OXYGEN_SHOWCASE_CONTENT}/"
    DESTINATION "${OXYGEN_INSTALL_DATA}/Content"
    COMPONENT Oxygen_data
    PATTERN
    "*.report.json"
      EXCLUDE
  )
  install(
    FILES
      "${OXYGEN_PROJECT_SOURCE_DIR}/tools/cli/BuildSelection.ps1"
    DESTINATION "${OXYGEN_INSTALL_DATA}/Tools"
    COMPONENT Oxygen_data
  )
endif()
