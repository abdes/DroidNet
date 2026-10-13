# Oxygen Engine Examples

This directory contains examples of the Oxygen Engine. They run on the
runtime host (`Oxygen.Host`); most add the **DemoShell** panels on top.

## Shared display and timing options

Async, InputSystem, LightBench, MultiView, Physics, RenderScene, TexturedCube and
VortexBasic share these runtime controls:

| Option                   | Meaning                                                                                                    |
| ------------------------ | ---------------------------------------------------------------------------------------------------------- |
| `--resolution 1920x1080` | Request an exact framebuffer size in physical pixels, accounting for DPI.                                  |
| `--fullscreen true`      | Use fullscreen; with `--resolution`, select a matching display mode at its highest available refresh rate. |
| `--fps 0`                | Disable the engine's frame-rate target.                                                                    |
| `--vsync false`          | Present without synchronization, using tearing when the DXGI swap chain supports it.                       |

The resolution separator accepts either `x` or `X` (`1920x1080` or `1920X1080`).
Fullscreen without a resolution retains desktop-mode behavior. An unavailable
resolution fails explicitly; it is never silently replaced with a different
size. Resolution selection requires a window and cannot be combined with
`--headless true`. Startup verifies the actual framebuffer size and mode before
creating rendering surfaces.

For a controlled, uncapped Full HD run:

```powershell
Oxygen.Examples.MultiView.exe --resolution 1920x1080 --fullscreen true --fps 0 --vsync false
```

These options, the graphics tooling switches (`--debug-layer`, `--aftermath`)
and frame capture come from `Oxygen.Host`'s `RuntimeCli`. `help-advanced`
lists the hidden groups. The host's `HostWindow` applies the window request
through the platform window API.

## Writing a demo

Every example runs on `Oxygen.Host`: the host owns the window, the per-view
framebuffers, view publishing, the console, the viewport toolbar (view mode,
show flags, stats, GPU work), pause and step. A demo supplies a scene, its
views and its own panels. VortexBasic shows the host alone; the others add
DemoShell for the shared panels, camera rig and settings.

### Startup

`main_impl.cpp` calls `RunDemo` (`DemoShell/Runtime/DemoMain.h`). It builds
the command line with the runtime, tooling and capture groups, applies it to
the host configuration and runs the host. The example adds its own option
groups and registers its modules:

```cpp
extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  examples::SettingsService::ForDemoApp();
  return examples::RunDemo(args,
    {
      .program = "physics",
      .about = "End-to-end physics integration demo",
      .application = "Physics Demo",
      .register_modules
      = [](AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(
          engine, std::make_unique<MainModule>(context));
        examples::RegisterDemoRenderer(engine, context);
      },
    });
}
```

`SettingsService::ForDemoApp()` must be called from the example itself: it
places the settings file next to the calling source.

### The module

Derive from `DemoModuleBase`, which is a `host::HostModule`. Return
`kDemoPhases`, plus any phase the demo needs, from `GetSupportedPhases`.
Create the `DemoShell` in `OnAttachedImpl` from `MakeShellConfig()`, then
override the host hooks:

- `BuildWindowProperties`: title and size, starting from the base result.
- Stage a scene with `Stage().Stage(...)` and its camera with
  `Stage().SetStagedMainCamera(...)`. The host publishes it at the next frame
  start and calls `OnScenePublished`; take the scene and camera there with
  `Stage().GetActiveScene()` and `Stage().TakePublishedMainCamera()`.
- `BuildViews`: add composition views to `views.views` and name the main one
  in `views.main`. An unset viewport covers the window.
- `AppendCompositionLayers`, when the demo draws extra texture layers.

The host gives the camera rig the main view through `OnMainViewReady`, and
the rig moves in `kInput` on wall-clock time, so the free camera still moves
while the engine is paused. Do not call the shell's update or draw yourself;
`DemoModuleBase` does it, below the toolbar.

### Panels

A panel control never changes state directly: it runs a command registered
through `DemoShell::GetPanelCommands()` (`AddAction`, `AddBool`, `AddFloat`,
`AddEnum`, ...) or sets a console variable. The command runs at the next
frame start, and the same command is available from the console and from a
remote client. Panel layout and presentation stay local. See
[Console.md](DemoShell/Console.md) for the commands.

Do not add readouts that the host already shows: frame, phase and GPU timings
belong to the toolbar's stats and GPU work overlay, and the view mode to its
view menu.
