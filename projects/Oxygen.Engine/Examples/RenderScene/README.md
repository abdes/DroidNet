# RenderScene

This example mounts a cooked `.pak` file, resolves a `SceneAsset` key (via the
PAK embedded browse index virtual paths or manual key entry), loads the cooked
scene through the engine `AssetLoader`, instantiates a runtime `scene::Scene`
hierarchy, and renders it using the standard single-view example pipeline.

The ImGui overlay is intentionally focused on PAK mounting and scene loading.

## Frame Capture CLI

`RenderScene` can configure backend frame capture at startup through the
graphics backend load config.

Default help keeps the capture surface short and user-facing:

- `--capture-provider off|renderdoc|pix`

Advanced and development-only flags are hidden from the default help. To show
them, run:

- `render-scene help-advanced`

Advanced capture flags:

- `--capture-load attached|search|path`
- `--capture-library <path-to-renderdoc.dll-or-WinPixGpuCapturer.dll>`
- `--capture-output <capture-file-template>`
- `--capture-from-frame <frame-number>`
- `--capture-frame-count <number-of-frames>`

`--capture-from-frame` is zero-based. Frame `0` is the first rendered frame,
so requesting frames `5` and `6` means `--capture-from-frame 5 --capture-frame-count 2`.

Examples:

- attach to an already running RenderDoc session and capture 3 frames starting at frame 120:
  `render-scene --capture-provider renderdoc --capture-load attached --capture-from-frame 120 --capture-frame-count 3`
- load `renderdoc.dll` from an explicit path:
  `render-scene --capture-provider renderdoc --capture-load path --capture-library C:/Tools/RenderDoc/renderdoc.dll`
- request a PIX startup capture using the installed PIX search path:
  `render-scene --capture-provider pix --capture-load search --capture-from-frame 1 --capture-frame-count 1 --capture-output out/build-ninja/pix-captures/render_scene`
- load `WinPixGpuCapturer.dll` from an explicit PIX install path:
  `render-scene --capture-provider pix --capture-load path --capture-library "C:/Program Files/Microsoft PIX/2602.25/WinPixGpuCapturer.dll" --capture-from-frame 1 --capture-frame-count 1 --capture-output out/build-ninja/pix-captures/render_scene`
- use PIX attached-only mode when the process was launched or attached through PIX already:
  `render-scene --capture-provider pix --capture-load attached`

For PIX, `--capture-output` is treated as a template prefix. The engine
generates a `.wpix` filename such as `render_scene_frame_0001.wpix`.
PIX startup frame-range capture also requires `--capture-from-frame > 0`.

The runtime also exposes dev-console commands when a frame-capture provider is
configured:

- `gfx.capture.status`
- `gfx.capture.frame`
- `gfx.capture.begin`
- `gfx.capture.end`

Provider-specific notes:

- `gfx.capture.status` prints both a human-readable summary and the raw provider
  state blob
- `gfx.capture.discard` and `gfx.capture.open_ui` are currently RenderDoc-only
- when PIX is active, unsupported commands return an explicit provider-specific
  error instead of a generic failure

## Using Python with RenderDoc

RenderDoc exposes two Python surfaces:

- `renderdoc`: the low-level replay API for opening captures, creating a `ReplayController`, and querying analysis data.
- `qrenderdoc`: the UI API used by the RenderDoc application itself.

For day-to-day capture analysis, the easiest path is to script the RenderDoc UI.
In distributed RenderDoc builds, this is also the most practical path because the
standalone `renderdoc` Python module is not normally shipped as a general-purpose
system Python package.

### Recommended workflow: script the RenderDoc UI

1. Generate a capture from `RenderScene`.
2. Write a small Python script.
3. Launch `qrenderdoc.exe` with the script and the `.rdc` file.

Example command on Windows:

```powershell
"C:/Program Files/RenderDoc/qrenderdoc.exe" --script .\analyze_capture.py .\capture_frame50.rdc
```

Inside a UI script, `renderdoc` and `qrenderdoc` are already available, and
`pyrenderdoc` is a global `CaptureContext` object.

Minimal example:

```python
filename = r"H:/captures/render_scene_frame50.rdc"

def dump_capture(controller):
  actions = controller.GetRootActions()
  print(f"top-level actions: {len(actions)}")
  for action in actions[:10]:
    print(f"eventId={action.eventId} name={action.customName}")

pyrenderdoc.LoadCapture(filename, renderdoc.ReplayOptions(), filename, False, True)
pyrenderdoc.Replay().BlockInvoke(dump_capture)
```

Important detail: when scripting the UI, replay work happens on RenderDoc's
replay thread, so use `BlockInvoke(...)` when you need access to the
`ReplayController`.

This style is a good fit for:

- counting and traversing actions
- locating passes of interest such as `View: Scene`, `LightCullingPass`, or `ToneMapPass`
- inspecting resources after a capture is already open in the UI
- building quick analysis helpers without reimplementing capture loading yourself

### Lower-level workflow: use the base replay API directly

If you have a compatible standalone `renderdoc` Python module available, you can
open and replay captures directly from Python without the UI.

```python
import renderdoc as rd

rd.InitialiseReplay(rd.GlobalEnvironment(), [])

cap = rd.OpenCaptureFile()
result = cap.OpenFile("render_scene_frame50.rdc", "", None)
if result != rd.ResultCode.Succeeded:
  raise RuntimeError(f"Couldn't open file: {result}")

if not cap.LocalReplaySupport():
  raise RuntimeError("Capture cannot be replayed on this machine")

result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
if result != rd.ResultCode.Succeeded:
  raise RuntimeError(f"Couldn't initialise replay: {result}")

print(f"top-level actions: {len(controller.GetRootActions())}")

controller.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
```

Use this lower-level path when you want fully scripted offline analysis, but be
aware that the Python module version must match the RenderDoc build exactly.

### Where to go next

Once you can open a capture and access a `ReplayController`, the next useful
automation steps are usually:

- iterate the action tree to find the pass or draw of interest
- inspect pipeline state around a chosen event
- fetch GPU counters for suspicious passes
- save textures or thumbnails to disk for regression checks

The RenderDoc Python docs are the right next stop for those tasks:

- `Getting Started (RenderDoc UI)`
- `Getting Started (python)`
- `Basic Interfaces`
- `renderdoc Examples`

## Content Source Behavior (PAK vs Loose Cooked)

The demo supports loading scenes from multiple cooked sources (PAK files and
loose cooked roots). The underlying content loader can mount multiple sources
at once, and the same `SceneAsset` key may exist in more than one source (for
example, a PAK and a loose cooked root built from the same content).

**Demo behavior:** when you select a scene from a source list, the demo
re-mounts that specific source immediately before the load request. This keeps
asset resolution deterministic and avoids ambiguous keys when the same asset
exists in multiple mounts.

In practice, this means:

- Picking a scene from the PAK list re-mounts the selected PAK.
- Picking a scene from the loose cooked list re-mounts the selected cooked
  root (via its `container.index.bin`).

This is intentional to ensure the `AssetLoader` resolves the requested key
against the source the user selected, even if other sources are currently
mounted.

## GLB to PakGen YAML

The helper script [Examples/RenderScene/glb_to_pak_spec.py](Examples/RenderScene/glb_to_pak_spec.py)
can generate a PakGen YAML spec from a `.glb`.

To avoid extremely large YAML files (hex-encoding doubles size and is slow to
parse), the default mode writes binary payloads (vertex/index buffers and
textures) to a sibling folder and references them via `file:` entries.

Example:

`python Examples/RenderScene/glb_to_pak_spec.py Examples/RenderScene/WaterBottle.glb bin/Oxygen/WaterBottle.yaml`

This produces:

- `bin/Oxygen/WaterBottle.yaml`
- `bin/Oxygen/WaterBottle_payload/*` (e.g. `.bin` for buffers, `.rgba8` for textures)

If you explicitly want to embed everything into YAML (slow, for debugging
only), use:

`python Examples/RenderScene/glb_to_pak_spec.py --data-mode hex <in.glb> <out.yaml>`

## One-command GLB -> PAK

If you want a single command that:

- generates the YAML spec + payload files, and
- builds the `.pak`

use [Examples/RenderScene/make_pak.py](Examples/RenderScene/make_pak.py).

Run it from the `Examples/RenderScene` directory:

`python .\make_pak.py .\glb\Tree1.glb`

You can also pass an existing PakGen YAML spec:

`python .\make_pak.py .\pak\Tree1.yaml`

Outputs:

- `pak/Tree1.yaml`
- `pak/Tree1_payload/*`
- `pak/Tree1.pak`
- `pak/Tree1.manifest.json`
