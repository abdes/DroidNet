# The engine content pak: the default material and the procedural shapes,
# ordinary assets under /Engine that every runtime mounts beneath its content.
# The cooker generates their descriptors from Oxygen.Data, cooks them into a
# loose root and packs that root with an embedded browse index.

include(RuntimeEnvironment)
oxygen_get_runtime_launcher(
  oxygen-cooker-importtool
  OXYGEN_ENGINE_CONTENT_LAUNCHER
)

# The engine content pak line keeps this identity across rebuilds.
set(OXYGEN_ENGINE_CONTENT_SOURCE_KEY "01a15078-4369-7e1a-aa40-479c4e2d163e")
set(
  _engine_content_config
  "$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${OXYGEN_WITH_ASAN}>>,Debug,Release>"
)
set(OXYGEN_ENGINE_CONTENT_WORK "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
# Repo-local runtimes find the pak where PathFinderConfig expects it.
set(
  OXYGEN_ENGINE_CONTENT_PAK
  "${OXYGEN_PROJECT_SOURCE_DIR}/bin/Oxygen/${_engine_content_config}/engine.pak"
)

add_custom_command(
  OUTPUT
    "${OXYGEN_ENGINE_CONTENT_PAK}"
  BYPRODUCTS
    "${OXYGEN_ENGINE_CONTENT_WORK}/engine.catalog.json"
  # Generated content: a tool or format change rebuilds it from scratch.
  COMMAND
    "${CMAKE_COMMAND}" -E rm -rf "${OXYGEN_ENGINE_CONTENT_WORK}"
  COMMAND
    "${OXYGEN_ENGINE_CONTENT_LAUNCHER}"
    "$<TARGET_FILE:oxygen-cooker-importtool>" --no-tui --no-color --quiet
    engine-content "${OXYGEN_ENGINE_CONTENT_WORK}/sources"
    "${OXYGEN_ENGINE_CONTENT_WORK}/cooked"
  # Geometry references the default material, so materials cook first.
  COMMAND
    "${OXYGEN_ENGINE_CONTENT_LAUNCHER}"
    "$<TARGET_FILE:oxygen-cooker-importtool>" --no-tui --no-color --quiet batch
    --manifest
    "${OXYGEN_ENGINE_CONTENT_WORK}/sources/materials.import-manifest.json"
  COMMAND
    "${OXYGEN_ENGINE_CONTENT_LAUNCHER}"
    "$<TARGET_FILE:oxygen-cooker-importtool>" --no-tui --no-color --quiet batch
    --manifest
    "${OXYGEN_ENGINE_CONTENT_WORK}/sources/geometry.import-manifest.json"
  COMMAND
    "${CMAKE_COMMAND}" -E make_directory
    "${OXYGEN_PROJECT_SOURCE_DIR}/bin/Oxygen/${_engine_content_config}"
  COMMAND
    "${OXYGEN_ENGINE_CONTENT_LAUNCHER}" "$<TARGET_FILE:oxygen-cooker-paktool>"
    build --quiet --no-color --loose-source
    "${OXYGEN_ENGINE_CONTENT_WORK}/cooked" --out "${OXYGEN_ENGINE_CONTENT_PAK}"
    --catalog-out "${OXYGEN_ENGINE_CONTENT_WORK}/engine.catalog.json"
    --content-version 1 --source-key "${OXYGEN_ENGINE_CONTENT_SOURCE_KEY}"
    --embed-browse-index
  DEPENDS
    oxygen-cooker
    oxygen-data
    oxygen-cooker-importtool
    oxygen-cooker-paktool
  COMMAND_EXPAND_LISTS
  VERBATIM
  COMMENT "Cooking the engine content pak ($<CONFIG>)"
)
add_custom_target(
  oxygen-engine-content
  ALL
  DEPENDS
    "${OXYGEN_ENGINE_CONTENT_PAK}"
)
# Cooks that reference /Engine assets read the pak path from the target, and
# scripts from the build tree, so this file stays its only owner.
set_target_properties(
  oxygen-engine-content
  PROPERTIES
    OXYGEN_ENGINE_CONTENT_PAK
      "${OXYGEN_ENGINE_CONTENT_PAK}"
)
file(
  GENERATE
  OUTPUT
  "${CMAKE_BINARY_DIR}/engine-content-pak-$<CONFIG>.txt"
  CONTENT "${OXYGEN_ENGINE_CONTENT_PAK}"
)

if(OXYGEN_INSTALL)
  oxygen_install_requires(oxygen-engine-content)
  # install-sdk: generated oxygen-engine-content
  install(
    FILES
      "${OXYGEN_ENGINE_CONTENT_PAK}"
    DESTINATION "${OXYGEN_INSTALL_DATA}"
    COMPONENT Oxygen_data
  )
endif()
