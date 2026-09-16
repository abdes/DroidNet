# RenderScene

RenderScene loads cooked scenes through Oxygen's native `AssetLoader` and renders
them with Vortex and Direct3D12. It supports loose cooked indexes, PAKs, model
import, scripts, and physics sidecars. No editor is required.

**Start here:** [build and run](#build-and-run), [UI workflows](#ui-workflows),
[verification](#verify-a-scene), [settings](#settings-and-repeatable-runs),
[troubleshooting](#troubleshooting), [source map](#source-map-for-contributors-and-agents).

## Build and run

Run commands from the **engine root**, `projects/Oxygen.Engine`, in a configured
Windows C++ development environment. Reuse the existing build tree.

```powershell
cmake --build out/build-ninja --config Debug --target oxygen-examples-renderscene oxygen-cooker-importtool oxygen-cooker-inspector oxygen-cooker-paktool
```

For a Visual Studio build tree, replace `out/build-ninja` with `out/build-vs`
in build and executable paths. Use tools from the same checkout/configuration.

### Refresh all authored example content

Close applications using the content. For a **full clean refresh**, back up
`Examples/Content/.cooked` and `Examples/Content/pak` outside those roots, then
start with an empty cooked root. Incremental cooking does not remove stale
outputs from previous formats or recipes.

```powershell
.\Examples\Content\cook_scenes.ps1 -All -NoTUI -ToolPath .\out\build-ninja\bin\Debug\Oxygen.Cooker.ImportTool.exe
.\Examples\Content\pak_content.ps1 -ToolPath .\out\build-ninja\bin\Debug\Oxygen.Cooker.PakTool.exe -DiagnosticsFile .\Examples\Content\pak\all.report.json
```

Require both commands to succeed and review their diagnostics. The current scope
is **13 manifests, 140 jobs, and 16 scenes**, including materials, textures,
scripts, inputs, and physics dependencies. Five raw FBXs and nine standalone
images are separate interactive demo inputs. See the [content guide](../Content/README.md)
for inventory and single-scene cooking. Preserve the script's stable PAK source
key when rebuilding.

### Reimport RenderScene's own content

`Examples/RenderScene/.cooked` is a separate, local library of previously
imported models. `Content/cook_scenes.ps1 -All` does **not** refresh it. Before
using that library after an importer or cooked-format change, reimport its
original glTF/GLB/FBX sources with the current native importer.

The local library refreshed on 2026-09-15 contains these four scenes. Original
files are external inputs; configure their machine-local locations in the source
list rather than assuming a path from another checkout:

| Cooked scene | Original input |
| --- | --- |
| `NewSponza_Main_glTF_003` | `NewSponza_Main_glTF_003.gltf` and its external buffers/textures. |
| `BurgerPiz` | `BurgerPiz.glb` |
| `town4new` | `town4new.glb` |
| `rgb_cubes` | `rgb_cubes.fbx` |

1. Close RenderScene and other consumers. Preserve settings; the reimport script
   stages a clean root and retains the previous local generation separately
   from the shared `Examples/Content` backup.
2. Verify each original source and every external dependency. An `.oscene`,
   `.ogeo`, or `.omat` file is cooked output, not a substitute source for this
   reimport.
3. Configure the source list and invoke the script below. It records the native
   manifest and reimports each original with the selected current policy.
4. Verify successful reports, the new index, all expected scenes and dependencies,
   and fresh content hashes **before launching against the replaced library**.
   Then select its own `container.index.bin` through **Library → Select Index**.

The recorded refresh policy is **BC7 compression with full mip chains**, all
content enabled, meter normalization, transform baking, generation of missing
normals/tangents, retained nodes, normalized naming, and content hashing. Color
textures use BC7 sRGB; data textures use linear BC7. This reimports original
source data with recorded current settings. Historical per-source settings were
not retained, so it is not a replay of unknown historical options.

Use [reimport_scenes.ps1](reimport_scenes.ps1), which invokes the existing native
ImportTool and Inspector. Create an ignored local source list from the
[schema-backed example](reimport-sources.example.json), then edit its paths:

```powershell
Copy-Item ./Examples/RenderScene/reimport-sources.example.json ./Examples/RenderScene/reimport-sources.local.json
```

From the engine root, run:

```powershell
./Examples/RenderScene/reimport_scenes.ps1 -SourceList ./Examples/RenderScene/reimport-sources.local.json
```

The defaults are **BC7 + Full**. For a different build, pass `-ToolPath`; Inspector
defaults to the same executable directory. `-CookedRoot` selects the live target.
Relative sources resolve against the source-list file; other relative arguments
resolve against the working directory. Use PowerShell 7.4 or newer.

| Parameter | Choices and effect |
| --- | --- |
| `-Compression` | `BC7` (default): BC7 sRGB color / linear BC7 data. `None`: uncompressed RGBA8 sRGB color / linear RGBA8 data; larger output, faster cooking. Both are explicit 8-bit texture policies, not source-format/HDR preservation. |
| `-MipPolicy` | `Full` (default): complete mip chain. `None`: base level only. `Max`: cap the chain at `-MaxMipLevels`. Mips improve minification and add storage. |
| `-MaxMipLevels` | Required only with `Max`; 1–255, including the base level. |
| `-BC7Quality` | `Fast`, `Default` (default), or `High`; compression time/quality tradeoff. Only valid with BC7. |
| `-ThreadPoolSize`, `-TextureWorkers` | Defaults 8 / 2. Increase together for more texture throughput when memory permits; workers cannot exceed the pool. Source jobs remain sequential. |
| `-WhatIf` | Read-only source/path/parameter preflight; no tools, cook, or publication. |

```powershell
./Examples/RenderScene/reimport_scenes.ps1 -SourceList ./Examples/RenderScene/reimport-sources.local.json -Compression None -MipPolicy Max -MaxMipLevels 5
```

The script validates source-list structure and original file existence, then uses
native manifest/schema preflight. It preserves the previous directory under the
build tree and cooks directly into `.cooked` with explicit virtual root
`/.cooked`. Successful completion requires successful
jobs, Inspector validation, expected scene outputs, descriptor sizes/SHA256, and
the requested texture formats/mip counts. The reserved fallback texture keeps
its native 1×1 RGBA8 format and is checked separately from imported textures.
Missing external model dependencies
are diagnosed by the native importer; any failure prevents publication.

The script never deletes old or failed generations. It rejects unsafe paths and
simultaneous runs against the same target. Close all consumers first. A failed
cook or validation moves failed output into the build-tree run and restores the
previous root. The output and build-tree backup must be on the same volume.

Each run retains its exact manifest, native report/logs, source/tool hashes,
output hashes, and `result.json` under `out/build-ninja/renderscene-reimport/`.
Failures exit nonzero and preserve evidence and failed output. Inspect reported warnings
and then perform native visual validation; successful cooking does not prove
rendered appearance. Run `Get-Help ./Examples/RenderScene/reimport_scenes.ps1 -Full`
for all parameters and error behavior.

Camera navigation has no separate default-input cook: DemoShell creates its
camera actions and mapping contexts in C++. Scene-authored input contexts are
cooked by their scene manifests and hydrated when their content source mounts.
The app's configured script source root is `Examples/Content`; retain source
scripts there when testing disk hot reload, or use `--hot-reload=false` for a
controlled cooked-content run.

### Launch a cooked scene

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --scene CubeScene --verify-hashes=true --directional-shadows conventional
```

This opens a native window until it is closed. Add `--frames 600 --fps 30` to
bound a smoke run. The limit includes startup/loading frames; it does not promise
600 frames of the loaded scene. `--help` shows common options; `help-advanced`
shows the full CLI:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe help-advanced
```

## Startup selection and source precedence

`--scene <token>` mounts `Examples/Content/.cooked/container.index.bin` from the
checkout compiled into the executable. It discards a restored active-scene load,
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

| Option | Purpose |
| --- | --- |
| `--verify-hashes=true` | Enable mounted-content hash verification. |
| `--cvars-archive <path>` | Use a separate persisted CVar archive. |
| `--directional-shadows conventional\|vsm` | Directional shadow policy; default is conventional. |
| `--hot-reload=false` | Disable disk script hot reload for a controlled run. |
| `--preview-sun=true\|false` | Add a preview sun when a loaded scene contains no directional light. Overrides the persisted preview setting for this process. |
| `--fps <rate>`, `--vsync=true\|false` | Frame pacing and synchronization. |
| `--startup-skybox <image>` | Equip a skybox; layout/output still come from demo settings. |
| `--debug-layer=true`, `--aftermath=true` | Diagnostic tooling; mutually exclusive. |

## UI workflows

Labels/actions below are verified against current source. **Live verification of
this refreshed-content workflow is pending.** Retain run evidence separately
from these operating instructions.

### Lighting imported models

RenderScene enables **preview sunlight by default**. After a scene finishes
building, it adds one `Preview Sun` if no directional-light component exists
anywhere in the hierarchy. This includes scenes loaded from the Library, restored
at startup, selected with `--scene`, or automatically loaded after an import.
The camera-only startup placeholder receives no sun.

The preview uses the demo's default directional light: 100,000 lux, a 0.53-degree
source angle, warm white color, shadows enabled, directed downward toward the
origin. It adds no atmosphere, sky light, fog, or exposure override. The light
belongs to that runtime scene; cooked files are unchanged. Existing directional
lights suppress creation even if hidden, disabled, or not tagged as a sun.
Activation preserves their authored values and shadow settings.

For intentionally dark, emissive, local-light-only, or authored-lighting checks,
disable the preview for that run:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --scene EmissiveScene --preview-sun=false
```

To change the default, set `render_scene.preview_sun.enabled` to `false` in
`demo_settings.json` while the app is closed. An explicit `--preview-sun` wins
over that setting without saving the command-line override. The setting takes
effect at the next launch. Normal **Environment → Sun** controls edit the light
in the active scene; a fresh scene load starts with its own authored or preview
light. A disabled preview setting does not disable an authored light.

The load log states `Preview sun added`, `skipped: directional light exists`, or
`disabled`, with the scene key. Check this before diagnosing a black surface.

### Load a loose index or PAK

1. Open **Content Loader → Library** in the side panel.
2. Use **Unload All** to remove previous mounts when isolating a source.
3. Choose **Select Index** and open `container.index.bin`, or **Select PAK** and
   open `Examples/Content/pak/all.pak`.
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

The default interactive output is `Examples/RenderScene/.cooked`; batch example
content goes to `Examples/Content/.cooked`. Persisted
`content.paths.last_cooked_output` can change the interactive destination.
These are separate generations.

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

| Example input | Intended use |
| --- | --- |
| `images/hdr_skybox_f32.hdr` | Equirectangular; RGBA16F or RGBA32F preserves HDR. |
| `images/4x3_skybox.png`, `images/skybox.png` | Horizontal Cross; ordinary LDR output. |
| `images/Wood.png`, `images/Marble.jpg`, `images/jellybeans1_s.jpg` | Ordinary 2D textures: [TexturedCube](../TexturedCube/README.md), **Texture Browser → Import Texture...**. |
| `images/debug_sky.png` | Labeled layout diagram with margins, not an exact cubemap atlas. |
| `images/singlepart.0002.exr`, `images/multipart.0002.exr` | Linear VFX decoder fixtures with extra channels/parts, not panoramas. |

A skybox load changes runtime lighting/background; it does not rebuild the shared
scene PAK. Record this when comparing captures. HDR-to-LDR tone mapping does not
preserve the source radiance range.

## Scenes to verify

Counts describe authored descriptors, before runtime script/physics changes.
Smoke-load all scenes in the refreshed library and use their expected outcomes
to choose representative captures. Use `--preview-sun=false` for this authored
lighting matrix, especially the emissive and point/spot-light scenes.

| Scene | Nodes / renderables | Expected coverage |
| --- | ---: | --- |
| `CubeScene` | 11 / 4 | Opaque, masked, blended, double-sided materials; directional, point, and spot lights. |
| `EmissiveScene` | 8 / 6 | RGB/textured/masked emission. No lights; the non-emissive reference can stay dark. |
| `CityEnvironmentValidation` | 316 / 310 | Material overrides, sun, atmosphere, fog, sky light. Authored camera: `(0,-760,145)`. |
| `InstancingTestScene` | 1051 / 1000 | Repeated geometry, 49 point lights, and a directional light. |
| `PointShadowValidation`, `SpotShadowValidation` | 6 / 3 each | Three surfaces and the named local-light shadow type; no directional sun. |
| `VsmTwoCubes` | 6 / 3 | Two cubes and ground with authored environment. |
| `VsmBug1LargePlane`, `VsmBug1LargeThinCube` | 4 / 1 each | Large thin-surface coverage; compare plane and thin solid. |
| `bottle-on-box` | 7 / 4 | Textured bottle, box, floor, sphere, and physics hydration. |
| `physics_domains`, `physics_domains_vsm_benchmark` | 23 / 17 each | Physics families, scripts, and camera behavior; require sidecar hydration. |
| `SceneProcCubes`, `multi_script_scene` | 3 / 0; 5 / 0 | Scripts create visible geometry after loading. |
| `backpack`, `chest` | Imported | glTF textures/geometry and rotation script sidecars. Check camera and lighting. |

Distinguish unlit front faces from culled backfaces. A single-sided backface is
absent. A double-sided backface can render with its normal reversed for lighting.
Mirrored instances should retain their visible authored exterior. A black front
face alone does not prove incorrect winding.

## Verify a scene

An exit code, empty viewport, or generic “Ready” label does not prove the requested
scene rendered.

| Stage | Evidence to retain |
| --- | --- |
| Selected | `Resolved startup scene override` with intended key **and source path**, or equivalent UI selection. |
| Loaded | `SceneLoader: Scene summary` with expected counts; no dependency/hash failures. |
| Built | Renderable/material/light/script attachment diagnostics and `Scene build staged successfully`. |
| Published | A subsequent `Published staged scene at frame-start` for that load. Startup fallback publication does not count. |
| Physics ready | If present: sidecar validation and `Deferred physics sidecar hydration completed`. |
| Rendered | Native viewport inspection and screenshot after loading, uploads, scripts, and exposure settle. |

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

`Examples/RenderScene/demo_settings.json` persists mounts, active scene, camera
poses keyed by camera name, exposure, render/debug modes, environment UI values,
and panels. Camera names can repeat across scenes. The shell reapplies
post-process settings after hydration. `--scene` does not reset these settings;
`--cvars-archive` isolates only CVars.

For a controlled run using the existing application:

1. Close RenderScene. Preserve original settings bytes and SHA256 outside the
   source directory; record whether the file originally existed.
2. Temporarily place minimal validation settings at that same path. Start each
   independent baseline from identical settings so a prior scene cannot supply
   a camera override or mount.
3. Pass a task-local `--cvars-archive` path and record interactive changes.
4. After process exit, archive its resulting settings, restore the original
   bytes, and verify SHA256. If no original existed, remove only the temporary
   settings file.

There is no CLI/environment override for the demo-settings path. Do not replace
it while the app is running: shutdown can save over it. Automatic exposure is
settings mode `2`; manual EV is mode `0`. Solid rendering is
`rendering.view_mode = "solid"`. Record exposure and allow adaptation when
judging brightness.

## GPU captures and native screenshots

The capture CLI records RenderDoc/PIX captures:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Examples.RenderScene.exe --scene CubeScene --frames 600 --fps 30 --capture-provider renderdoc --capture-load search --capture-from-frame 300 --capture-frame-count 1 --capture-output .\out\renderscene-cube
```

Frame 300 is an example, not a readiness guarantee. First measure when the scene
is live, then choose a capture frame and sufficient post-capture frames.
`--capture-provider` accepts `off|renderdoc|pix`; `--capture-load` accepts
`attached|search|path`. For `path`, supply `--capture-library <runtime-library>`.
Indices are zero-based; scheduled PIX capture must start after frame 0.

For a native appearance check without opening RenderDoc, extract the original
image embedded in an `.rdc` using the reusable
[thumbnail export tool](../../tools/vortex/ExportRenderDocThumbnail.ps1):

```powershell
./tools/vortex/ExportRenderDocThumbnail.ps1 -CapturePath ./out/renderscene-cube_capture.rdc -OutputPath ./out/renderscene-cube.png
```

Use the actual `.rdc` filename from the capture log. The tool runs
`renderdoccmd thumb`, validates the PNG, and records its dimensions, hashes, and
native diagnostics. `-MaxSize 0` keeps the largest embedded image available;
`-MaxSize 1024` applies a size limit. This image was recorded by the native app;
its resolution and compression can differ from the viewport. It is suitable for
visual checks, not exact pixel parity. Failed export preserves an existing PNG
and exits nonzero.

For pass/resource analysis, the existing
[analysis wrapper](../../tools/shadows/Invoke-RenderDocUiAnalysis.ps1) now uses
RenderDoc's supported startup Python mode before the UI opens:

```powershell
./tools/shadows/Invoke-RenderDocUiAnalysis.ps1 -CapturePath ./out/renderscene-cube_capture.rdc -UiScriptPath tools/vortex/DumpRenderDocActions.py -PassName Actions -ReportPath ./out/cube-actions.txt
```

It retains existing `UiScriptPath` callers, uses isolated child-process settings,
and requires the analysis and replay-handle cleanup to finish successfully.
Import errors, invalid captures, callback failures, and timeouts fail the command.
It needs no PySide2, mouse, keyboard, or open RenderDoc window. Direct manual
`--ui-python` workflows remain available and leave the user's UI open.

The native console (grave-accent key) exposes `gfx.capture.status`,
`gfx.capture.frame`, `gfx.capture.begin`, `gfx.capture.end`,
`gfx.capture.discard`, and `gfx.capture.open_ui`. Ctrl+Shift+P opens the command
palette. Native screenshots prove visible appearance; GPU captures support
pass/resource inspection. Retain loader/hydration diagnostics alongside both.

Native preview-sun verification on 2026-09-15 passed with original-source
`rgb_cubes` on/off images at identical camera/exposure, authored-light preservation
in `CubeScene`, and persisted opt-out in `EmissiveScene`. RGB's Debug/hash-verified
dependency load took about 57 seconds; capture after the loaded-scene publication,
not merely after startup. See the
[validation record](../../design/vortex/plan/renderscene-preview-sun.md) for scope
and remaining example-refresh checks.

## Troubleshooting

| Symptom | Check first |
| --- | --- |
| Missing/wrong scene | Index exists; exact token; logged source path; persisted mounts/duplicate generations. |
| Successful exit before content appears | Frame budget expired during loading. Increase it and verify publication before capture. |
| Black, tiny, or offscreen scene | Camera, near/far planes, exposure, solid/debug mode, and the preview-sun decision in the load log. An existing disabled/hidden directional light intentionally prevents injection. |
| `No physics sidecar found ... (scene-only load)` | Expected for scenes without physics; not a load failure. |
| `has no resolved sun directional light` | Expected for the startup placeholder, preview-disabled scenes without a sun, or directional lights without a sun/environment role. Inspect the loaded scene's preview decision. |
| Script scene summary has zero renderables | Check script attachment, compilation, and runtime creation. |
| Sidecar/dependency/schema/hash failures | Use matching built/cooked generations and review diagnostics. Do not mix old/new outputs to conceal failures. |
| Skybox placeholder | Layout/output, valid dimensions, import status, and resource key. |
| Selecting a file unexpectedly starts a cook | Sources import; Library entries load already-cooked scenes. |

## Source map for contributors and agents

Use live sources before changing the workflow:

| Responsibility | Source |
| --- | --- |
| CLI, startup CVars, engine modules | [main_impl.cpp](main_impl.cpp) |
| Mounts, startup matching, publication, deferred physics | [MainModule.cpp](MainModule.cpp) |
| Scene construction, cameras, materials, scripts, physics | [SceneLoaderService.cpp](../DemoShell/Services/SceneLoaderService.cpp) |
| Source/Library UI and import requests | [ContentLoaderPanel.cpp](../DemoShell/UI/ContentLoaderPanel.cpp), [ContentVm.cpp](../DemoShell/UI/ContentVm.cpp) |
| Settings location/lifecycle | [SettingsService.cpp](../DemoShell/Services/SettingsService.cpp), [DemoShell.cpp](../DemoShell/DemoShell.cpp) |
| Camera persistence/reset | [CameraSettingsService.cpp](../DemoShell/Services/CameraSettingsService.cpp) |
| Environment UI and cubemap binding | [EnvironmentDebugPanel.cpp](../DemoShell/UI/EnvironmentDebugPanel.cpp), [SkyboxService.cpp](../DemoShell/Services/SkyboxService.cpp) |
| Preview sun creation and authored sun binding | [DefaultSceneLighting.cpp](../DemoShell/Services/DefaultSceneLighting.cpp), [EnvironmentSettingsService.cpp](../DemoShell/Services/EnvironmentSettingsService.cpp) |
| Capture options | [FrameCaptureCliOptions.h](../Common/FrameCaptureCliOptions.h) |
| Content recipes | [Content README](../Content/README.md), [scene manifests](../Content/scenes) |

Editor milestone M08 plans automated editor/native parity validation. That
protocol is not implemented by this demo today; this guide documents current
native loading and inspection.
