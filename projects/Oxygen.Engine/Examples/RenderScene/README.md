# RenderScene

RenderScene loads cooked scenes through Oxygen's native AssetLoader and renders
with Vortex and Direct3D12. Use it to inspect materials, lighting, scripts and
physics, or to explore imported models without an editor.

[Start](#start) · [Load content](#load-content) · [Lighting](#lighting-and-preview-sun) ·
[Verify a scene](#verify-a-scene) · [Repeatable runs](#settings-and-repeatable-runs) ·
[Troubleshooting](#troubleshooting)

## Start

From the installed SDK root:

```powershell
./RenderScene.cmd
./RenderScene.cmd --scene SdkMaterials
./RenderScene.cmd --scene CityEnvironmentValidation
```

The SDK launcher finds its executable and working directory without changing
PATH; it can also be invoked by full path. First launch selects Lantern with an
HDR sky. Subsequent launches can restore your saved scene and settings. Copy the
complete SDK, including `bin`, `lib`, `include`, `share` and launchers when moving
it. SDK-local paths follow the copied tree; external originals remain external.
The showcase needs a writable directory, Windows/D3D12 and a compatible GPU.

From an engine checkout, use a configured build tree and run from
`projects/Oxygen.Engine`:

```powershell
cmake --build out/build-ninja --config Release --target oxygen-examples-renderscene
./out/build-ninja/bin/Release/Oxygen.Examples.RenderScene.exe --scene CubeScene
```

Close the running executable before relinking it. Use `out/build-vs` for the
existing Visual Studio tree. The [Content workflow](../Content/README.md) owns
native tool selection, scene cooking, model imports, verification, packaging and
cleanup. All content scripts live there.

## Load content

### Open a library and scene

1. Open **Content Loader → Library** in the side panel.
2. Use **Unload All** to remove previous mounts when isolating a source.
3. Choose **Select Library** for the shared main index or a retained `.import.json`
   record, or **Select PAK** for a packaged library. Paths are in the Content guide.
4. Check **Mounted Items**, then use **Search scenes...** under **Library Scenes**
   and click a scene. Hover its entry for the path, key, and source details.
5. Review **Diagnostics** and progress. **Cancel Scene Load** cancels an active
   request.

For an external scene, also mount its required dependency sources. Keep duplicate
versions of the same asset out of a baseline test.

### Import FBX, GLB, or glTF

1. Open **Content Loader → Sources**.
2. Under **Content Root**, use **Browse** and enable **FBX**, **GLB**, or **GLTF**,
   then refresh discovery. Alternatively use **Select File**.
3. Review **Import Configuration** and **Texture Tuning**. Selecting a discovered
   source starts its import.
4. Watch progress and **Diagnostics**. The imported index joins the library.
   **Sources → Workflow Settings → Auto-load scene after import** controls
   whether a scene is requested automatically.

Interactive model imports publish retained records and immutable generations
under the shared Content directory. The [Content workflow](../Content/README.md)
owns the paths, replay settings and cleanup rules.

Transform baking can emit geometry variants named `__baked_<node>` while preserving
scene placement. `mesh.transform_bake_retained` explains why a transform remains
on a node, such as attachments, animation/skinning, or non-invertibility. Read
that diagnostic before assuming every node should become identity.

### Images and cubemaps

RenderScene's **Select File** picker is for models. For images use
**Environment → Sky Sphere → Source: Cubemap → Skybox Loader**:

1. Select an image using **Browse...** or **Path**.
2. Set **Layout** and **Output**. **Face Size** controls equirectangular conversion
   resolution. **Load Skybox** cooks and binds the cubemap at runtime.
3. Check status and **Cubemap ResourceKey**. A placeholder means no usable cubemap
   is bound. Inspect the background and sky lighting as appropriate.

The SDK includes `Content/images/showcase/Sky.hdr`. The additional examples below
are checkout inputs; their paths are relative to Content.

| Example input                                                      | Intended use                                                                                              |
| ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------- |
| `images/hdr_skybox_f32.hdr`                                        | Equirectangular; RGBA16F or RGBA32F preserves HDR.                                                        |
| `images/4x3_skybox.png`, `images/skybox.png`                       | Horizontal Cross; ordinary LDR output.                                                                    |
| `images/Wood.png`, `images/Marble.jpg`, `images/jellybeans1_s.jpg` | Ordinary 2D textures: [TexturedCube](../TexturedCube/README.md), **Texture Browser → Import Texture...**. |
| `images/debug_sky.png`                                             | Labeled layout diagram with margins, not an exact cubemap atlas.                                          |
| `images/singlepart.0002.exr`, `images/multipart.0002.exr`          | Linear VFX decoder fixtures with extra channels/parts, not panoramas.                                     |

A skybox load changes runtime lighting/background; it does not rebuild the shared
scene PAK. Record this when comparing captures. HDR-to-LDR tone mapping does not
preserve the source radiance range.

## Startup selection and source precedence

`--scene <token>` mounts the shared main library from the SDK or source checkout. It discards a restored active-scene load,
then selects the first matching entry among **all available mounted scenes**.
Matching is case-insensitive and accepts a virtual path, filename stem, asset
key, or substring.

- Prefer a full virtual path or key; short substrings can match multiple scenes.
- Persisted PAK/index mounts still participate. Duplicate keys or paths can exist
  in loose and PAK generations; check the resolved **source path** in the log.
- For a controlled test, mount one intended source. To test `all.pak`, launch
  without `--scene` and use the Library controls below.
- A scene load mounts a missing source, refreshes a changed source, and preserves
  the cache for an unchanged mounted source. Selection does not always remount
  the source or force it above every other mount.
- Without `--scene`, the UI can restore its last scene. If none loads, the app
  runs with a fallback scene/camera. That is not proof of a content-scene load.

`--scene` also seeds these startup CVars to `true`:
`vtx.local_fog.enable`, `vtx.local_fog.render_into_volumetric_fog`,
`vtx.volumetric_fog.directional_shadows`, and
`vtx.volumetric_fog.temporal_reprojection`. Account for these when comparing
CLI-selected and UI-selected runs.

| Option                                    | Purpose                                                                                                                                                 |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--verify-hashes=true`                    | Enable mounted-content hash verification.                                                                                                               |
| `--cvars-archive <path>`                  | Use a separate persisted CVar archive.                                                                                                                  |
| `--directional-shadows conventional\|vsm` | Directional shadow policy; default is conventional.                                                                                                     |
| `--hot-reload=false`                      | Disable disk script hot reload for a controlled run.                                                                                                    |
| `--environment-profile <key>`             | Select `scene`, `custom`, `outdoor-sunny`, `outdoor-cloudy`, `foggy-daylight`, `outdoor-dawn`, or `outdoor-dusk` for this run.                          |
| `--preview-sun=true\|false`               | Independently opt in to preview sunlight for any profile. Reuse a scene directional light, or create one, only when no authored sun designation exists. |
| `--fps <rate>`, `--vsync=true\|false`     | Frame pacing and synchronization.                                                                                                                       |
| `--startup-skybox <image>`                | Equip a skybox through Custom; layout/output come from demo settings. Conflicts with an explicit non-Custom profile.                                    |
| `--debug-layer=true`, `--aftermath=true`  | Diagnostic tooling; mutually exclusive.                                                                                                                 |

## Lighting and preview sun

Open **Environment**. **Profile** selects the environment; **Preview sun** directly
below it is an independent opt-in available to every profile. Changing the
checkbox leaves the selected profile unchanged. A new settings file starts in
**Use Scene**, with preview off.

| Profile                                                                                       | What it uses                                                                                                                                                                  |
| --------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Use Scene**                                                                                 | The loaded scene's authored environment and sun values. Returning here restores them after profile edits. Preview sunlight is the only opt-in environment addition.           |
| **Outdoor Sunny**, **Outdoor Cloudy**, **Foggy Daylight**, **Outdoor Dawn**, **Outdoor Dusk** | Premade sun, atmosphere, sky-light, fog, and exposure settings. A premade does not opt in to preview sunlight.                                                                |
| **Custom**                                                                                    | Your saved environment edits. Selecting another profile retains these values; selecting Custom restores them. Without a saved Custom, it starts from the current environment. |

Editing an environment property switches to **Custom** and saves that edit.
Selecting a profile in the UI saves the selection for subsequent launches.
Preview has its own saved preference, `render_scene.preview_sun.enabled`.

The help below **Preview sun** explains the decision for the loaded scene:

| State                                      | Meaning and effect                                                                                                                                                                                                                                                    |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Unavailable                                | The named scene light already has `IsSunLight` or an explicit **Primary**/**Secondary** atmosphere role. Hidden or switched-off authored suns still reserve that assignment. RenderScene does not replace it. The control is also unavailable until a scene is ready. |
| Available, unchecked; named candidate      | Checking borrows that scene directional light as the preview sun. Its intensity, color, direction, visibility, and shadow settings remain authored in Use Scene. Visible directionals are preferred when choosing a candidate.                                        |
| Available, unchecked; no directional light | Checking creates a temporary node named `Preview Sun`.                                                                                                                                                                                                                |
| Checked                                    | The help names the actual light and whether it was borrowed or created. Unchecking restores a borrowed light's previous sun-role and world-lighting flags, or removes the temporary node. An off or zero-intensity light is identified in the help.                   |

Borrowing sets the light's world-lighting, sun, and environment contribution flags
and assigns **Primary** for the preview. It does not silently brighten a
zero-intensity light or unhide a node. With a premade or Custom selected, that
profile supplies the active sun settings. If the scene already designates a sun,
the profile uses it without creating a preview light.

A newly created preview starts at 100,000 lux, warm white, a 0.53-degree source
angle, and shadows with bias 0.03. It follows Oxygen's Z-up basis and points
downward toward the scene origin. The preview checkbox itself adds no atmosphere,
sky light, fog, cubemap, or exposure override. To enable an atmosphere yourself,
select **Custom**, expand **Sky Atmosphere**, and enable it; opt in to preview
sunlight separately if the scene has no designated sun.

These changes affect the running scene only. Source models and cooked files stay
unchanged. Loading another scene reevaluates its own sun/candidate; the
camera-only startup placeholder receives no preview. **Sun** controls edit the
currently selected light. When no sun is selected, the panel explains how to
enable one instead of presenting ineffective controls. Expand **Runtime details**
when inspecting renderer state.

Both options are in the public **Environment** group of `--help`. CLI selections
apply to the current process without saving a different profile, preview
preference, environment, or exposure. Subsequent explicit UI edits are saved.
Profile keys are exact; display labels and aliases are not accepted.

```powershell
./out/build-ninja/bin/Release/Oxygen.Examples.RenderScene.exe --scene EmissiveScene --environment-profile scene --preview-sun=false
./out/build-ninja/bin/Release/Oxygen.Examples.RenderScene.exe --scene rgb_cubes --environment-profile outdoor-sunny --preview-sun=true
```

The log records the resolved startup choices and the preview decision, including
whether a requested preview became active and which source was used. Check the
panel's named light and this decision before diagnosing a dark surface.

## Scenes to verify

Counts describe authored descriptors, before runtime script/physics changes.
Smoke-load all scenes in the refreshed library and use their expected outcomes
to choose representative captures. Use `--environment-profile scene` and
`--preview-sun=false` for this authored lighting matrix, especially the emissive
and point/spot-light scenes.

| Scene                                              | Nodes / renderables | Expected coverage                                                                     |
| -------------------------------------------------- | ------------------: | ------------------------------------------------------------------------------------- |
| `CubeScene`                                        |              11 / 4 | Opaque, masked, blended, double-sided materials; directional, point, and spot lights. |
| `EmissiveScene`                                    |               8 / 6 | RGB/textured/masked emission. No lights; the non-emissive reference can stay dark.    |
| `CityEnvironmentValidation`                        |           316 / 310 | Material overrides, sun, atmosphere, fog, sky light. Authored camera: `(0,-760,145)`. |
| `InstancingTestScene`                              |         1051 / 1000 | Repeated geometry, 49 point lights, and a directional light.                          |
| `PointShadowValidation`, `SpotShadowValidation`    |          6 / 3 each | Three surfaces and the named local-light shadow type; no directional sun.             |
| `VsmTwoCubes`                                      |               6 / 3 | Two cubes and ground with authored environment.                                       |
| `VsmBug1LargePlane`, `VsmBug1LargeThinCube`        |          4 / 1 each | Large thin-surface coverage; compare plane and thin solid.                            |
| `bottle-on-box`                                    |               7 / 4 | Textured bottle, box, floor, sphere, and physics hydration.                           |
| `physics_domains`, `physics_domains_vsm_benchmark` |        23 / 17 each | Physics families, scripts, and camera behavior; require sidecar hydration.            |
| `SceneProcCubes`, `multi_script_scene`             |        3 / 0; 5 / 0 | Scripts create visible geometry after loading.                                        |
| `backpack`, `chest`                                |            Imported | glTF textures/geometry and rotation script sidecars. Check camera and lighting.       |

`VsmTwoCubes`, `CityEnvironmentValidation` and `physics_domains` keep the fog cutoff disabled. Fog end distance bounds horizontal
integration; cutoff distance removes fog beyond the resulting ray length. A
nearby nonzero cutoff therefore creates a hard angular boundary in the sky,
exposing the dark lower atmosphere around this small ground mesh. Use cutoff
only when intentionally excluding distant scenery from fog.

Distinguish unlit front faces from culled backfaces. A single-sided backface is
absent. A double-sided backface can render with its normal reversed for lighting.
Mirrored instances should retain their visible authored exterior. A black front
face alone does not prove incorrect winding.

## Verify a scene

Check each stage when confirming a load or investigating an empty viewport.

| Stage         | Evidence to retain                                                                                               |
| ------------- | ---------------------------------------------------------------------------------------------------------------- |
| Selected      | `Resolved startup scene override` with intended key **and source path**, or equivalent UI selection.             |
| Loaded        | `SceneLoader: Scene summary` with expected counts; no dependency/hash failures.                                  |
| Built         | Renderable/material/light/script attachment diagnostics and `Scene build staged successfully`.                   |
| Published     | A subsequent `Published staged scene at frame-start` for that load. Startup fallback publication does not count. |
| Physics ready | If present: sidecar validation and `Deferred physics sidecar hydration completed`.                               |
| Rendered      | Native viewport inspection and screenshot after loading, uploads, scripts, and exposure settle.                  |

Also check camera motion, resize, and scene switching. Compare loose/PAK loads in
isolation with identical settings: scene identity, counts, diagnostics, and
appearance. Retain cook/package reports, run command, build/configuration,
content hashes, logs, settings used, and captures.

The loader chooses the first perspective camera, then an orthographic camera.
Without either, it creates `MainCamera` at `(10,10,10)` looking at the origin;
this does not frame arbitrary model bounds. **Camera Controls → Reset Camera
Position** restores the initial pose. Fly controls: WASD, Q/E down/up, right mouse
drag to look, Shift to boost, Space for horizontal-plane lock, mouse wheel for
speed.

## Settings and repeatable runs

The installed SDK saves `demo_settings.json` beside this guide. Checkout runs save
it in `Examples/RenderScene`. It stores mounts, active scene, camera poses by name,
exposure, environment, rendering/debug choices and panels. Camera names can recur
across scenes. **Reset Camera Position** restores the loaded camera's initial pose;
`--scene` leaves the other saved preferences in place. Automatic exposure uses
mode `2`, manual EV mode `0`; solid rendering is `rendering.view_mode = "solid"`.
Allow exposure to settle before judging brightness.

CLI profile/preview options apply to the current run. Explicit UI edits persist.
`--cvars-archive <path>` selects a separate CVar archive; it does not redirect demo
settings. Keep real settings unchanged while another RenderScene process can save
them on shutdown.

For a repeatable application run, close RenderScene, save the settings file outside
the source tree, and use identical camera, exposure, source mounts and environment
for each comparison. Restore the saved bytes afterward. Scene scripts resolve
against the Content source root; use `--hot-reload=false` when evaluating cooked
script behavior. Camera navigation input is created by DemoShell, while authored
scene input contexts are cooked and hydrated with their content.

### Run the native widget tests

A build configured with `OXYGEN_BUILD_UI_TESTS` supports isolated settings:

1. Copy `demo_settings.json` into a fresh directory under `out`.
2. Set `OXYGEN_UI_TEST_OUTPUT` to that directory and `OXYGEN_UI_TEST_SETTINGS` to
   the copied file. Set `OXYGEN_UI_TEST_FILTER` to a test name, such as
   `retained_library` for a settings file with retained records configured.
3. Run the existing executable with `--cvars-archive` pointing into that directory.
4. Require a successful exit and a nonempty, passing `results.xml`. Remove those
   environment variables from the shell after the run.

`retained_library` checks the restored scene, one mounted generation per retained
record, renderer availability and usable sky lighting. `ibl_persist_and_replace`
and `ibl_reopen` run as separate processes against the same copied settings; the
second restores the saved scene/profile without CLI overrides. Without an isolated
settings override, widget runs disable settings writes.

## Capture a loaded scene

From the engine root, schedule a RenderDoc capture after the scene has loaded:

```powershell
./out/build-ninja/bin/Release/Oxygen.Examples.RenderScene.exe --scene CubeScene --frames 600 --fps 30 --capture-provider renderdoc --capture-load search --capture-from-frame 300 --capture-frame-count 1 --capture-output ./out/renderscene-cube
```

Choose the capture frame from the load diagnostics; startup time varies with the
scene. Frame indices are zero-based. Providers are `off`, `renderdoc` and `pix`;
load modes are `attached`, `search` and `path`. For `path`, provide
`--capture-library <runtime-library>`. Scheduled PIX capture starts after frame 0.

Export the original embedded image using the actual capture filename:

```powershell
./tools/vortex/ExportRenderDocThumbnail.ps1 -CapturePath ./out/renderscene-cube_capture.rdc -OutputPath ./out/renderscene-cube.png
```

`-MaxSize 0` preserves the largest embedded image; `-MaxSize 1024` limits it.
The image's resolution/compression may differ from the viewport. Use RenderDoc
resource/pass inspection for detailed rendering analysis:

```powershell
./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath ./out/renderscene-cube_capture.rdc -UiScriptPath tools/vortex/DumpRenderDocActions.py -PassName Actions -ReportPath ./out/cube-actions.txt
```

The wrapper uses RenderDoc's startup Python mode and isolated child settings.
It reports import, capture, callback and timeout failures. Manual `--ui-python`
analysis is also available. Keep logs, settings and captures together under `out`.

The console opens with the grave-accent key and exposes `gfx.capture.status`,
`gfx.capture.frame`, `gfx.capture.begin`, `gfx.capture.end`, `gfx.capture.discard`
and `gfx.capture.open_ui`. Ctrl+Shift+P opens the command palette.

## Troubleshooting

| Symptom                                          | Check first                                                                                                                                                                                                                                                                    |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Missing/wrong scene                              | Index exists; exact token; logged source path; persisted mounts/duplicate generations.                                                                                                                                                                                         |
| Successful exit before content appears           | Frame budget expired during loading. Increase it and verify publication before capture.                                                                                                                                                                                        |
| Black, tiny, or offscreen scene                  | Camera, near/far planes, exposure, solid/debug mode, selected profile, and the named preview decision. Hidden/off authored sun designations block preview; an untagged directional may instead be borrowed without changing its visibility or authored intensity in Use Scene. |
| Preview sun is unavailable                       | Read the reason beneath the checkbox. A named authored sun or Primary/Secondary assignment reserves the role even when hidden or off. Wait for a scene load if no scene is ready.                                                                                              |
| A premade has no sunlight                        | A profile does not enable preview. Check Preview sun if offered; otherwise inspect the named authored sun and its visibility.                                                                                                                                                  |
| `No physics sidecar found ... (scene-only load)` | Expected for scenes without physics; not a load failure.                                                                                                                                                                                                                       |
| `has no resolved sun directional light`          | Expected for the startup placeholder, preview-disabled scenes without a sun, or directional lights without a sun/environment role. Inspect the loaded scene's preview decision.                                                                                                |
| Script scene summary has zero renderables        | Check script attachment, compilation, and runtime creation.                                                                                                                                                                                                                    |
| Sidecar/dependency/schema/hash failures          | Use matching built/cooked generations and review diagnostics. Recook incompatible content through the Content workflow.                                                                                                                                                        |
| Skybox placeholder                               | Layout/output, valid dimensions, import status, and resource key.                                                                                                                                                                                                              |
| Selecting a file unexpectedly starts a cook      | Sources import; Library entries load already-cooked scenes.                                                                                                                                                                                                                    |

## Contributor source map

These paths are available in the source checkout:

| Responsibility                                   | Source                                                                                                                                                                                                                             |
| ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CLI, startup CVars and engine modules            | [main_impl.cpp](main_impl.cpp)                                                                                                                                                                                                     |
| Mounts, scene selection and deferred physics     | [MainModule.cpp](MainModule.cpp)                                                                                                                                                                                                   |
| Scene construction and hydration                 | [SceneLoaderService.cpp](../DemoShell/Services/SceneLoaderService.cpp)                                                                                                                                                             |
| Library/import UI and desired record persistence | [ContentLoaderPanel.cpp](../DemoShell/UI/ContentLoaderPanel.cpp), [ContentVm.cpp](../DemoShell/UI/ContentVm.cpp)                                                                                                                   |
| Settings lifecycle and camera persistence        | [SettingsService.cpp](../DemoShell/Services/SettingsService.cpp), [CameraSettingsService.cpp](../DemoShell/Services/CameraSettingsService.cpp)                                                                                     |
| Environment UI and cubemap binding               | [EnvironmentDebugPanel.cpp](../DemoShell/UI/EnvironmentDebugPanel.cpp), [SkyboxService.cpp](../DemoShell/Services/SkyboxService.cpp)                                                                                               |
| Preview eligibility and restoration              | [PreviewSunController.cpp](../DemoShell/Services/PreviewSunController.cpp), [DefaultSceneLighting.cpp](../DemoShell/Services/DefaultSceneLighting.cpp)                                                                             |
| Profiles and authored-environment restoration    | [EnvironmentVm.cpp](../DemoShell/UI/EnvironmentVm.cpp), [EnvironmentSettingsService.cpp](../DemoShell/Services/EnvironmentSettingsService.cpp), [EnvironmentSceneSnapshot.cpp](../DemoShell/Services/EnvironmentSceneSnapshot.cpp) |
| Capture CLI                                      | [FrameCaptureCliOptions.h](../Common/FrameCaptureCliOptions.h)                                                                                                                                                                     |
| Content production and cleanup                   | [Content README](../Content/README.md)                                                                                                                                                                                             |
