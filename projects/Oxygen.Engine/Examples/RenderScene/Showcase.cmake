# RenderScene's installed showcase: the examples tagged `showcase` in
# Examples.json, staged as share/oxygen holds them. `Showcase/` is the examples
# project reduced to the tag; `RenderScene/Tools/` are RenderScene's tools,
# which cook it into RenderScene's own `.cooked/` and `.build/`, never into the
# project. The build selection script sits in `Tools/` beside them, as in an
# installed SDK, so the staged tools run as they do there.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(OXYGEN_SHOWCASE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/showcase")
set(OXYGEN_SHOWCASE_CONFIGS "${CMAKE_CONFIGURATION_TYPES}")
if(NOT OXYGEN_SHOWCASE_CONFIGS)
  set(OXYGEN_SHOWCASE_CONFIGS "${CMAKE_BUILD_TYPE}")
endif()
set(
  OXYGEN_SHOWCASE_PREPARE
  "${CMAKE_CURRENT_SOURCE_DIR}/Tools/internal/prepare_showcase.py"
)
set(
  OXYGEN_SHOWCASE_BUILD_SELECTION
  "${OXYGEN_PROJECT_SOURCE_DIR}/tools/cli/BuildSelection.ps1"
)
foreach(_config IN LISTS OXYGEN_SHOWCASE_CONFIGS)
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${OXYGEN_SHOWCASE_PREPARE}" --source
      "${OXYGEN_EXAMPLES_SOURCE_DIR}/Showcase" --stage
      "${OXYGEN_SHOWCASE_ROOT}/${_config}" --tag showcase --inputs
      "${OXYGEN_SHOWCASE_ROOT}/${_config}/inputs.txt"
    COMMAND_ERROR_IS_FATAL ANY
  )
  file(
    COPY
      "${OXYGEN_SHOWCASE_BUILD_SELECTION}"
    DESTINATION "${OXYGEN_SHOWCASE_ROOT}/${_config}/Tools"
  )
  file(
    STRINGS
    "${OXYGEN_SHOWCASE_ROOT}/${_config}/inputs.txt"
    OXYGEN_SHOWCASE_INPUTS
  )
endforeach()
set(OXYGEN_SHOWCASE_STAGE "${OXYGEN_SHOWCASE_ROOT}/$<CONFIG>")
set(OXYGEN_SHOWCASE_RENDERSCENE "${OXYGEN_SHOWCASE_STAGE}/RenderScene")
set_property(
  DIRECTORY
  APPEND
  PROPERTY
    CMAKE_CONFIGURE_DEPENDS
      ${OXYGEN_SHOWCASE_INPUTS}
      "${OXYGEN_SHOWCASE_PREPARE}"
      "${OXYGEN_SHOWCASE_BUILD_SELECTION}"
)
include(RuntimeEnvironment)
oxygen_get_runtime_launcher(oxygen-cooker-importtool OXYGEN_SHOWCASE_LAUNCHER)
# The content tools require PowerShell 7.4 or newer.
find_program(OXYGEN_PWSH_EXECUTABLE NAMES pwsh REQUIRED)
set(OXYGEN_SHOWCASE_STAMP "${OXYGEN_SHOWCASE_ROOT}/cooked-$<CONFIG>.stamp")
add_custom_command(
  OUTPUT
    "${OXYGEN_SHOWCASE_STAMP}"
  BYPRODUCTS
    "${OXYGEN_SHOWCASE_RENDERSCENE}/.cooked/Content/container.index.bin"
    "${OXYGEN_SHOWCASE_RENDERSCENE}/.build/pak/examples.pak"
    "${OXYGEN_SHOWCASE_RENDERSCENE}/.build/pak/examples.catalog.json"
    "${OXYGEN_SHOWCASE_RENDERSCENE}/.build/pak/examples.manifest.json"
  # This tree is a generated SDK build product. A tool/format change must
  # rebuild it from authoring inputs, never append to an older cooked index.
  COMMAND
    "${CMAKE_COMMAND}" -E rm -f "${OXYGEN_SHOWCASE_STAMP}"
  COMMAND
    "${CMAKE_COMMAND}" -E rm -rf "${OXYGEN_SHOWCASE_RENDERSCENE}/.cooked"
    "${OXYGEN_SHOWCASE_RENDERSCENE}/.build"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" "${OXYGEN_PWSH_EXECUTABLE}" -NoLogo -NoProfile
    -ExecutionPolicy Bypass -File
    "${OXYGEN_SHOWCASE_RENDERSCENE}/Tools/cook.ps1" -All -NoTUI -ToolPath
    "$<TARGET_FILE:oxygen-cooker-importtool>" -EnginePak
    "$<GENEX_EVAL:$<TARGET_PROPERTY:oxygen-engine-content,OXYGEN_ENGINE_CONTENT_PAK>>"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" "${OXYGEN_PWSH_EXECUTABLE}" -NoLogo -NoProfile
    -ExecutionPolicy Bypass -File "${OXYGEN_SHOWCASE_RENDERSCENE}/Tools/pak.ps1"
    -ToolPath "$<TARGET_FILE:oxygen-cooker-paktool>" -EnginePak
    "$<GENEX_EVAL:$<TARGET_PROPERTY:oxygen-engine-content,OXYGEN_ENGINE_CONTENT_PAK>>"
  COMMAND
    "${OXYGEN_SHOWCASE_LAUNCHER}" "$<TARGET_FILE:oxygen-cooker-inspector>"
    validate "${OXYGEN_SHOWCASE_RENDERSCENE}/.cooked/Content"
  COMMAND
    "${CMAKE_COMMAND}" -E touch "${OXYGEN_SHOWCASE_STAMP}"
  DEPENDS
    "${OXYGEN_SHOWCASE_PREPARE}"
    ${OXYGEN_SHOWCASE_INPUTS}
    oxygen-cooker
    oxygen-data
    oxygen-cooker-importtool
    oxygen-cooker-paktool
    oxygen-cooker-inspector
    oxygen-engine-content
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
oxygen_install_requires(oxygen-showcase-content)
# install-sdk: generated oxygen-showcase-content
install(
  CODE
    "
    if(NOT EXISTS \"${OXYGEN_SHOWCASE_RENDERSCENE}/.cooked/Content/container.index.bin\"
       OR NOT EXISTS \"${OXYGEN_SHOWCASE_RENDERSCENE}/.build/pak/examples.pak\")
      message(FATAL_ERROR \"Build the selected configuration before installing the SDK: showcase content is missing.\")
    endif()
  "
  COMPONENT Oxygen_data
)
# install-sdk: generated oxygen-showcase-content
install(
  DIRECTORY
    "${OXYGEN_SHOWCASE_STAGE}/Showcase/"
  DESTINATION "${OXYGEN_INSTALL_DATA}/Showcase"
  COMPONENT Oxygen_data
)
# install-sdk: generated oxygen-showcase-content
install(
  DIRECTORY
    "${OXYGEN_SHOWCASE_RENDERSCENE}/"
  DESTINATION "${OXYGEN_INSTALL_DATA}/RenderScene"
  COMPONENT Oxygen_data
  PATTERN
  "*.report.json"
    EXCLUDE
)
# install-sdk: static
install(
  FILES
    "${OXYGEN_SHOWCASE_BUILD_SELECTION}"
  DESTINATION "${OXYGEN_INSTALL_DATA}/Tools"
  COMPONENT Oxygen_data
)
