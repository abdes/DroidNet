# RenderScene: explore the Oxygen SDK

RenderScene is the SDK's interactive showcase. It lets you inspect the supplied
scenes, change lighting and materials, load cooked content, and import your own
assets through the same native pipeline used by Oxygen applications.

## Start the showcase

From the SDK root:

```powershell
.\RenderScene.cmd
```

Or invoke `RenderScene.ps1`. Both commands locate the installed executable and
establish its working directory without changing PATH. You can invoke either
launcher by its full path from another directory.

The initial scene is the supplied Lantern glTF model with an HDR sky. Use the
Library panel to select other shipped scenes. The showcase saves settings beside
this README, so this directory must be writable. Your existing settings are not
replaced when the SDK is installed again.

## Choose a scene

```powershell
.\RenderScene.cmd --scene SdkMaterials
.\RenderScene.cmd --scene CityEnvironmentValidation
.\RenderScene.cmd --scene physics_domains
.\RenderScene.cmd --scene multi_script_scene
.\RenderScene.cmd --scene PointShadowValidation
.\RenderScene.cmd --scene SpotShadowValidation
```

`--scene` mounts this SDK's loose cooked content and selects a matching scene.
Prefer a complete name or virtual path if several names match. Persisted PAK and
index mounts can contain other versions of an asset; use Library to remove
unwanted sources before comparing results. With no explicit selection, your saved
scene can be restored on subsequent launches.

The [content guide](../Content/README.md) describes the samples and the complete
edit, cook and pack workflow. All required tools and authoring schemas are bundled.

## Explore the panels

- **Library/content:** mount a loose cooked index or a PAK, browse its scenes,
  and choose the scene to load.
- **Import:** import your own FBX, glTF/GLB or image. Choose an output directory
  and retain the generated content if you want to reuse it later.
- **Camera:** frame the scene and adjust the view and projection.
- **Lighting/environment:** compare scene-authored lighting with an environment
  preset or a custom sky. Preview sunlight can illuminate imported scenes that
  contain no directional light.
- **Diagnostics:** inspect rendering and engine information while experimenting.

The HDR sky and marble/wood source maps are under `share/oxygen/Content/images/showcase`.
Keep HDR inputs in a floating-point workflow when comparing environment lighting.

## Useful launch options

```powershell
.\RenderScene.cmd --help
.\RenderScene.cmd help-advanced
.\RenderScene.cmd --scene Lantern --frames 600 --fps 30
.\RenderScene.cmd --scene CityEnvironmentValidation --directional-shadows conventional
```

A frame limit includes startup and loading frames. Give large custom imports
enough time to finish before judging a run. `--verify-hashes=true` enables content
hash verification. `--environment-profile` accepts `scene`, `custom`,
`outdoor-sunny`, `outdoor-cloudy`, `foggy-daylight`, `outdoor-dawn` and `outdoor-dusk`.

Changing your last-viewed scene or lighting can affect the next launch. Preserve
`demo_settings.json` before experimenting with clean settings. CVar state and
script caches also live in this portable showcase area. Read-only installation
locations are not supported yet.

## Move the SDK

Copy or extract the complete SDK directory, including `bin`, `lib`, `include`,
`share` and the launchers. The installed showcase resolves its resources from
that tree. No Oxygen checkout, Conan cache or developer shell is needed to run it.
SDK-local content and skybox paths in saved settings follow the copied tree.
Your own external assets still need to be available at the locations you selected.

## Troubleshooting

- **Missing DLL:** restore the SDK's complete `bin` directory; do not copy only
  the showcase executable. Use the launcher belonging to this SDK.
- **No scene appears:** select a scene in Library and inspect the selected source.
  An empty fallback view does not mean a content scene loaded successfully.
- **Cooked content is incompatible:** recook with this SDK's ImportTool, then
  rebuild the PAK if you use one. See the content guide.
- **Unexpected lighting:** check the selected environment profile and preview-sun
  setting; scene-authored and override lighting are different choices.
- **Settings cannot be saved:** move the complete SDK to a writable location.

RenderScene requires the supported Windows/D3D12 platform and a compatible GPU.

## Engine contributors

Build-tree runs retain settings beside the source at
`Examples/RenderScene/demo_settings.json`, and load source-tree content and shaders.
See `Examples/RenderScene/DEVELOPMENT.md` in the source checkout for build and run instructions.
