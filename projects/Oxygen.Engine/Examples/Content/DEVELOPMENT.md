# Source-checkout development reference

These notes concern engine contributors working from a checkout. SDK recipients
should use [README.md](README.md). Historical validation records describe the
referenced work, not a certification of a newly installed SDK.

# Native example content

The native ImportTool cooks the authored manifests in `scenes/` into the shared
`.cooked` root. PakTool packages that root into `pak/all.pak` and its catalog
and manifest. RenderScene can load either the loose root or the PAK.
Scene descriptors use version 7; older cooked scenes must be recooked.
Atmosphere sources use explicit per-light slots, and scripted light changes use
validated whole-candidate updates.

## Cook and package

From this directory, using tools built in the current engine checkout:

```powershell
.\cook_scenes.ps1 -h
.\pak_content.ps1 -Help
.\cook_scenes.ps1 -All -NoTUI
.\pak_content.ps1 -DiagnosticsFile .\pak\all.report.json
```

`cook_scenes.ps1` also supports `-Scene <folder>` and selects an available built
tool from CMake presets when `-ToolPath` is omitted, preferring
Release, ordinary builds, then Ninja. `-Preset`, `-BuildTree`, and `-Config` can
constrain that selection.
It reads presets without configuring or building the engine. Manifest and
descriptor validation belongs to the native ImportTool and its current
schemas; the wrapper does not duplicate schema version checks.

For a full refresh after a cooked-format change, close applications using the
content, preserve the previous `.cooked` and `pak` generations in a backup,
and cook into a clean `.cooked` root before repackaging. Cooking over an old
root is incremental and does not prove stale outputs were removed. Keep the
stable PAK source key used by `pak_content.ps1` when rebuilding this content
line.

## Scope of `-All`

The script processes every `scenes/*/import-manifest.json` in name order.
Currently, 13 manifests contain 140 jobs and produce 16 scene assets:

- bottle-on-box
- CityEnvironmentValidation
- CubeScene
- EmissiveScene
- InstancingTestScene
- multi_script_scene
- physics_domains and physics_domains_vsm_benchmark
- PointShadowValidation
- SceneProcCubes
- backpack and chest
- SpotShadowValidation
- VsmBug1LargePlane and VsmBug1LargeThinCube
- VsmTwoCubes

The jobs include geometry, materials, textures, buffers, scripts, input assets,
collision shapes, physics materials, and scene sidecars. Shared dependencies
may occur in several manifests. Distinct authored assets must use distinct
virtual paths: the instancing scene uses `GeoInstancedCube`, and the physics
domains scene uses `Physics/Materials/physics_domains_ground.opmat`, so cooking
them cannot replace the cube or bottle scene's authored defaults.

The two GLBs in `glb/` belong to the rotating-gltf manifest. The five FBXs in
`fbx/` and nine standalone images in `images/` are separate native import and
runtime demonstration inputs; the scene script does not enumerate those
directories. Texture roles and skybox projection settings are selected by
their consuming demo or an explicit import recipe. `-All` does not rebuild
PAKs; run `pak_content.ps1` after cooking.

`make_pak.py` accepts PakGen YAML specifications. It is not the native
manifest-based example-content refresh entry point.
It uses the Python API under `src/Oxygen/Cooker/Tools/PakGen`; `python make_pak.py
--help` (or `-h`) works without loading PakGen. The PowerShell workflows offer
`-Help`/`-h` without requiring scene arguments or initialized build trees.

Native tools are resolved and invoked by the same shared script library as
`oxyrun`. The workflows supply target names and argument arrays; they do not
construct executable locations or bypass the launcher for `-ToolPath` overrides.

## Imported asset names

The native import naming service keeps normalized asset spelling and assigns
numeric suffixes when mesh, material, or scene names collide after ASCII case
folding. For example, `MetalGrey` and `Metalgrey` remain separate materials
named `M_MetalGrey` and `M_Metalgrey_1`. This rule applies on every platform
to prevent case-only storage collisions within an import session on Windows.
Namespaces and already occupied suffixes participate in collision checks.
Scene-node display names retain their case-sensitive naming behavior.
