# Runtime host, play sessions and debug UI

Status: **draft for owner review, 2026-10-11; reviewed 2026-10-12; I1
validated 2026-10-12; H1 built and unit-tested 2026-10-12; E1 validated
2026-10-12 (ahead of H2, so the host has a running consumer; the owner ran
VortexBasic windowed, resized and fullscreen); H2 built and tested
2026-10-12; H3 built: catalog, main-view variables, widgets and toolbar
2026-10-12, GPU work overlay 2026-10-13; P1 built and its gate passed 2026-10-13; R1 built and tested 2026-10-13;
E2 and E3 built 2026-10-13, awaiting validation in the running
examples**. Owner
decisions are recorded in [§10](#10-owner-decisions) with the review's
position beside each. [§11](#11-open-problems-for-review) gives a definite
answer to every problem and marks the ones only the owner can close.

Summary:

- A new engine library, `Oxygen.Host`, takes over the startup, window, view
  publishing and scene hand-off code copied today across eight examples and
  DemoShell ([§3](#3-oxygenhost)).
- Pause, frame step and play sessions are built into `AsyncEngine`
  ([§4](#4-pause-frame-step-and-play-sessions)). Ending a session tears down
  everything the session created and leaves the engine running, idle and
  ready for the next session, so control returns to whoever started it: the
  player's own toolbar, the editor through the control server, or later an in-process
  editor game mode.
- The debug UI is small and shared by the player and DemoShell: a UE-style
  toolbar with view mode, show flags and stats, Play/Pause/Step/Stop at the
  top centre, and the mandatory console and command palette
  ([§5](#5-runtime-debug-ui)). View mode and show flags apply to the main
  game view only.
- `Oxygen.Player` is the host, the toolbar, the launch file and its
  lifecycle events; nothing else ([§7](#7-oxygenplayer-composition)).
- Delivery order: ImGui moves out of Vortex into an `Oxygen.ImGui` module →
  `Oxygen.Host` → `Oxygen.Player` → VortexBasic without
  DemoShell → commands and remote control → DemoShell → the other examples
  ([§9](#9-delivery-order-and-exit-gates)).

The player's launch file, its commands and events, and scene start state
stay in
[scene instantiation and player](scene-instantiation-and-player.md).

## Review (Fable, 2026-10-12)

Every `file:line` in [§1](#1-findings) and every file named in
[§11](#11-open-problems-for-review) was re-read against the tree on
2026-10-12. Paths are relative to `projects/Oxygen.Engine`.

**Confirmed**

- [§1](#1-findings): every citation holds; two rows corrected (below).
- [§4.1](#41-how-other-engines-do-it): the engine comparison is accurate.
- [§4.2](#42-engine-api): pause, step and sessions belong in `AsyncEngine`
  with a small API; the API gained request semantics and a failure path.
- [§4.4](#44-session-lifecycle), P-1: session hooks as `EngineModule`
  virtuals are the right shape; `OnSessionBegin` now returns `bool`.
- [§4.4](#44-session-lifecycle), P-2: a fresh Lua state per session is
  right. Every binding pack keeps its state in the Lua registry
  (`Scripting/Bindings/LuaBindingCommon.h:124-134`,
  `Bindings/Packs/Core/EventsBindings.cpp:835-849`); `src/Oxygen/Scripting`
  has no file-scope mutable statics. Nothing process-wide needs a reset.
- [§5.2](#52-main-view-console-variables): the host, not Vortex, owns the
  main view's mode and show flags; Vortex owns the catalog. This matches
  UE, where view mode and show flags are viewport-client state and the
  `ShowFlag.*` console variables are global overrides for every view.
- [§7](#7-oxygenplayer-composition) and
  [§9](#9-delivery-order-and-exit-gates): composition and order.

**Changed**

- [§1](#1-findings): the pause row. `kFixedSimulation` already runs zero
  steps when the clock is paused (`Core/Time/SimulationClock.cpp:93-97`);
  only `kGameplay` still runs. Added rows for the demo camera rig, the
  loader's mount swap, physics ownership and the pause usage in MultiView.
- [§3.1](#31-startup-and-shutdown): `HostConfig` no longer repeats fields
  that `EngineConfig` already carries (`Config/EngineConfig.h:136`,
  `Config/GraphicsConfig.h:106-109`). Request threading is stated.
- [§3.5](#35-hostmodule): the main view is marked by `ViewId`, not by index.
- [§4.2](#42-engine-api): all calls are thread-safe requests applied in
  `kFrameStart`; `Step` implies `Pause`; a failed begin is defined.
- [§4.3](#43-pause-and-step-semantics): a phase table for the paused
  frame; a module-level tick-when-paused opt-in; how edges behave; `Step`
  mechanics through a new `SimulationClock::StepFixed()`; variable
  timestep and physical delta on stepped frames.
- [§4.4](#44-session-lifecycle): the scene outlives every end hook; the
  host cancels instantiation through its nursery (no generation counter);
  the main view and its framebuffers survive the session with an exposure
  discontinuity; a failed instantiation ends the session; process exit
  does not run session hooks; the host decides the post-session scene.
- [§5.2](#52-main-view-console-variables): `lit` clears the view overrides;
  the toolbar shows the effective mode.
- [§7](#7-oxygenplayer-composition): gaps a shipped player still has.
- [§9](#9-delivery-order-and-exit-gates): gates that catch the regressions
  the design is exposed to (late loader callbacks, leaks across sessions,
  stale content, held keys across resume).
- [§11](#11-open-problems-for-review): P-1 to P-5 answered.
- [Scene doc](scene-instantiation-and-player.md#control-and-events): lists
  the session commands and lifecycle values this document relies on; SIP-5
  separates `ngin.session.end` from `ngin.quit`.

**Recommended differently from [§10](#10-owner-decisions)**

- _Pause and frame step: simple API._ Keep the API, but add one virtual,
  `EngineModule::TicksWhenPaused()`, now
  ([§4.3](#43-pause-and-step-semantics)).
  Without it a shipped game cannot run a pause menu from Luau, and every
  mature engine has the opt-in (UE `bTickEvenWhenPaused`, Godot
  `process_mode`, Unity's unscaled `Update`). The script-slot flag that
  would use it is authoring data and stays deferred.
- _DemoShell settings unchanged._ Keep the settings, but remove the
  Diagnostics panel's render and debug mode so `host.view_mode` alone owns
  the main view's mode ([§5.2](#52-main-view-console-variables)). Done in
  E2.
- _Physics hydration: no._ Confirmed for the player. The session hook
  design still names who creates and destroys a physics world, because
  there is no physics engine module today ([§4.4](#44-session-lifecycle)).

**Owner decisions on the review, 2026-10-12**

- `SimulationClock::StepFixed()` and `EngineModule::TicksWhenPaused()`:
  approved, both now. The `runs_when_paused` script slot flag stays
  deferred.
- Remount policy: always remount on `ngin.session.begin` from the editor
  ([P-4](#p-4-recooked-content-in-a-warm-player)).
- Warm player: approved; the editor LLD is updated
  ([P-5](#p-5-editor-flow-with-a-warm-player)).
- DemoShell's render mode goes through `host.view_mode` from E2
  ([§5.2](#52-main-view-console-variables)).

## 1. Findings

Paths are relative to `projects/Oxygen.Engine` unless they start with
`Oxygen.Editor.Interop`.

| Finding                                                                                                                                                                                                                                                                                                                                                                                                 | Evidence                                                                                                                                                                                                                           |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The same startup and shutdown is written nine times: Platform, backend load, command queues, `AsyncEngine`, module registration, `co::Run` event loop traits, `LastWindowClosed → Stop`, teardown in a fixed order, three catch blocks. The copies differ in log text, options and the module list. Devices and Platform are not engine examples and are not counted.                                   | `Examples/*/main_impl.cpp` (320–667 lines each; RenderScene `:67-191`, `:434-546`); `src/Oxygen/EditorInterface/EngineRunner.cpp:43-269`                                                                                           |
| Window plumbing is generic but lives in DemoShell. `AppWindow` owns the window, surface, per-frame framebuffers, resize and close; it needs four fields of `DemoAppContext`. Its framebuffers still carry a depth texture, which the editor already removed from its composite targets (ED-M08 plan, M08.V0 R2).                                                                                        | `Examples/DemoShell/Runtime/AppWindow.cpp:244-265`; `design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md` R2                                                                                                     |
| View publishing is generic but cannot be used without a `DemoShell`: per-view scene and composite framebuffers, `PublishRuntimeCompositionView`, release of inactive views, `RegisterRuntimeComposition` onto the window.                                                                                                                                                                               | `Examples/DemoShell/Runtime/DemoModuleBase.cpp:191-195`, `:308-586`, `:515`                                                                                                                                                        |
| Scene hand-off is repeated in seven examples: stage a scene, publish it at frame start, `FrameContext::SetScene`. Mutation collection starts in one file and ends in another.                                                                                                                                                                                                                           | `Examples/DemoShell/Internal/SceneControlBlock.h:85-105`; `Services/SceneLoaderService.cpp:2168`; RenderScene `MainModule.cpp:386-422` and six others                                                                              |
| Runtime and graphics-tooling options (fps, vsync, resolution, debug layer, Aftermath, RenderDoc/PIX capture) have no demo dependency. `EngineConfig` already embeds `GraphicsConfig`, which carries `headless`, `enable_imgui`, `enable_vsync` and `frame_capture`.                                                                                                                                     | `Examples/Common/DemoCli.h`, `FrameCaptureCli*.h`; `src/Oxygen/Config/EngineConfig.h:136`; `Config/GraphicsConfig.h:106-109`                                                                                                       |
| ImGui frames are engine-owned. Vortex creates the context, begins the frame at frame start, composites the overlay and ends the frame. An app names the window and draws in `OnGuiUpdate`.                                                                                                                                                                                                              | `src/Oxygen/Vortex/Renderer.cpp:1425-1459`, `:1918`, `:2005-2032`, `:2328`; `Vortex/Internal/ImGuiRuntime.cpp:102-177`                                                                                                             |
| `Oxygen.ImGui` holds the console panel, command palette, Spectrum style and icon fonts. It has no toolbar or menu widgets and links engine, platform, console and graphics-common, not Vortex.                                                                                                                                                                                                          | `src/Oxygen/ImGui/Console/*`, `Styles/*`; `ImGui/CMakeLists.txt:92-100`                                                                                                                                                            |
| A view carries its own optional render mode, shader debug mode, feature mask (lighting, shadows, environment, translucency, diagnostics, volumetrics, ground grid) and atmosphere and fog switches. `ShaderDebugModeRegistry` lists every debug mode with name, family, required capabilities and an unsupported reason. A view override wins over the renderer-global mode (`value_or(default_mode)`). | `Vortex/CompositionView.h:52-57`, `:79-94`, `:245-253`; `Renderer.cpp:4544-4547`, `:2864`; `Vortex/Diagnostics/ShaderDebugModeRegistry.h:55-72`                                                                                    |
| The editor maps its pane view modes onto those view settings in its own table.                                                                                                                                                                                                                                                                                                                          | `Oxygen.Editor.Interop/src/EditorModule/EditorModule.cpp:140-189`, `EditorView.h:99-111`                                                                                                                                           |
| The simulation clock can pause and scale time, but the engine never fills `ModuleTimingData::is_paused` or `time_scale`, so `FrameContext::IsGamePaused()` is always false. While the clock is paused, `ExecuteFixedSteps` returns zero steps, so `kFixedSimulation` does not run; `kGameplay` still runs for every module with a zero game delta, so scripts get `on_update` and input action events.  | `Core/Time/SimulationClock.h:97-103`; `SimulationClock.cpp:69-97`; `Engine/TimeManager.cpp:52-62`; `Engine/AsyncEngine.cpp:701-712`, `:1003-1017`; `Core/FrameContext.cpp:234-242`; `Scripting/Module/ScriptingModule.cpp:609-618` |
| MultiView pauses the clock directly for its exposure proofs. RenderScene moves the DemoShell camera rig in `kInput` with the game delta; MultiView moves it in `kGameplay`. Both cameras therefore freeze when the clock is paused today.                                                                                                                                                               | `Examples/MultiView/MainModule.cpp:206`, `:698-704`, `:1117`; `Examples/RenderScene/MainModule.cpp:1153-1161`                                                                                                                      |
| The scripting module ends every script instance (`on_destroy`) when the frame's scene changes, including to none. Its Lua state lives from `OnAttached` to shutdown, so script globals survive a scene change. The compiled bytecode lives in the engine-owned compilation service (L1 memory and L2 file cache), not in the Lua state.                                                                 | `Scripting/Module/ScriptingModule.cpp:978-998`, `:387`, `:426-450`; `Engine/Scripting/ScriptCompilationService.h:37-41`                                                                                                            |
| The input system can remove mapping contexts and actions and records which contexts are active, but nothing records which ones a scene or a script added. Per-frame action edges reset at every frame start; Triggered edges for fixed-step consumers are latched until consumed.                                                                                                                       | `Input/InputSystem.h:89-116`; `Input/InputSystem.cpp:128-133`; `Input/Action.h:176-186`                                                                                                                                            |
| ImGui captures keyboard and mouse it wants, and the input system then drops those events, so game input is suppressed while a menu or the console has focus. The console hotkeys are read from ImGui regardless of capture.                                                                                                                                                                             | `ImGui/PlatformBackend.cpp` (`HandleEvent`, `OnKey`, `OnMouseMotion`); `Platform/SDL/InputEvents.cpp:320`; `Examples/DemoShell/UI/DemoShellUi.cpp:154-171`                                                                         |
| The asset loader can prepare a replacement mount set off-thread, wait for in-flight reads and swap sources atomically; the swap detaches the old sources' cache entries. Residency pins are RAII handles.                                                                                                                                                                                               | `Content/IAssetLoader.h:382-405`, `:579-581`                                                                                                                                                                                       |
| There is no physics engine module. Worlds are created and destroyed through `IWorldApi` by whoever hydrates physics (the Physics example, DemoShell).                                                                                                                                                                                                                                                   | `Physics/System/IWorldApi.h:44-46`; `Examples/Physics/MainModule.h`                                                                                                                                                                |
| `Instantiate` awaits loader callbacks through a shared pending block, so a callback that fires after its awaiter is gone touches only that block.                                                                                                                                                                                                                                                       | `SceneInstantiation/SceneInstantiation.cpp:286-311`                                                                                                                                                                                |

## 2. Boundaries

| Home                                   | Contents                                                                                                                                                                                                    | Depends on                                                 |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| Engine (`AsyncEngine`, `EngineModule`) | Pause, frame step, play sessions; session hooks and the tick-when-paused opt-in on modules; the command registry                                                                                            | as today                                                   |
| `Oxygen.Control` (R1)                  | Command dispatcher and the run-state commands, event log, control server ([commands and remote control](commands-and-remote-control.md))                                                                    | Commands, Console, Engine                                  |
| Core (`SimulationClock`)               | `StepFixed()` ([§4.3](#43-pause-and-step-semantics))                                                                                                                                                        | as today                                                   |
| Scripting, Input                       | Their session-scoped state, cleared at session end                                                                                                                                                          | as today                                                   |
| Vortex                                 | View-mode catalog; GPU work summary and bucket catalog; surface layer sources                                                                                                                               | as today, without ImGui                                    |
| `Oxygen.Host` (new)                    | Startup and shutdown, host window, view publisher, scene stage, host module base, runtime command-line options, viewport toolbar and its main-view console variables, ImGui layer adapter, GPU work overlay | Platform, Engine, Graphics, Loader, Vortex, Console, ImGui |
| `Oxygen.ImGui`                         | ImGui module (context, window binding, frame lifecycle, style and fonts); generic widgets: dropdown menu, toolbar button, play controls, stats overlay, GPU work chart, console host                        | as today (no Vortex)                                       |
| `Oxygen.Player` (new)                  | Launch file, player module and its events, failure and "Stopped" message                                                                                                                                    | Host, Control, SceneInstantiation, Scripting, Input        |
| DemoShell                              | Unchanged showcase code: panels, settings service and settings file, scene picker and content loader, environment editor and presets, skybox, preview sun, fallback camera, UI tests                        | Host, ImGui                                                |

Rejected: putting the toolbar binding in `Oxygen.ImGui`. It needs the
Vortex catalog and the console; `Oxygen.ImGui` stays a generic widget
library.

Rejected: moving DemoShell's panel framework (`DemoPanel`, `PanelRegistry`,
`SidePanel`, `PanelSideBar`) into the engine. The player has no panels, so
DemoShell would be its only consumer.

## 3. `Oxygen.Host`

Namespace `oxygen::host`; library target `oxygen::host`; folder
`src/Oxygen/Host`. Every piece is extracted from existing code.

| Piece                   | Source                                                             |
| ----------------------- | ------------------------------------------------------------------ |
| `HostConfig`, `RunHost` | example `main_impl.cpp` bootstrap                                  |
| `HostWindow`            | `AppWindow`                                                        |
| `ViewPublisher`         | `DemoModuleBase` publish and composite half                        |
| `SceneStage`            | `SceneControlBlock`, `ActiveScene`                                 |
| `HostModule`            | `DemoModuleBase` without DemoShell                                 |
| `RuntimeCli`            | `DemoCli.h`, `FrameCaptureCli*.h`                                  |
| `ViewportToolbar`       | new ([§5.4](#54-viewport-toolbar))                                 |
| `ImGuiLayer`            | DemoShell's interim `ImGuiLayer` ([§5.3](#53-oxygenimgui-widgets)) |
| `GpuWorkOverlay`        | new ([§5.5](#55-gpu-work-overlay))                                 |

### 3.1 Startup and shutdown

```cpp
struct HostWindowConfig {
  std::optional<Extent<std::uint32_t>> resolution; // framebuffer pixels
  bool fullscreen { false };
};

struct HostConfig {
  EngineConfig engine;        // application, fps, frames, loader,
                              // scripting, path finder, timing, and
                              // engine.graphics: debug layer, aftermath,
                              // vsync, imgui, frame capture, headless
  PlatformConfig platform;    // headless, thread pool size (capped by
                              // hardware concurrency)
  HostWindowConfig window;
  graphics::BackendType backend { graphics::BackendType::kDirect3D12 };
  console::ConsoleStartupPlan startup_cvars;
};

//! Registers the application's modules after the engine runs.
using RegisterModules = std::function<void(AsyncEngine&, HostContext&)>;

//! Runs the engine until it stops; returns the process exit code.
OXGN_HOST_NDAPI auto RunHost(HostConfig config,
  const RegisterModules& register_modules) -> int;

//! Registers a module the application cannot run without; throws when the
//! engine rejects it.
OXGN_HOST_API auto RegisterRequiredModule(AsyncEngine& engine,
  std::unique_ptr<engine::EngineModule> module) -> void;
```

`EngineConfig` already carries `GraphicsConfig` and its `headless` flag
(`Config/EngineConfig.h:136`, `GraphicsConfig.h:106-109`); `HostConfig`
does not repeat them. `RunHost` sets `engine.graphics.enable_imgui` unless
headless, because the toolbar and console need it, and `backend` becomes
`kHeadless` when `platform.headless` is set. When `enable_imgui` is set,
`RunHost` registers the ImGui module and its `ImGuiLayer`
([§5.3](#53-oxygenimgui-widgets)) before `register_modules`.

`HostContext` is the generic part of `DemoAppContext`: platform, graphics
(weak), engine, queue strategy, input system, `running`, `headless` and the
window options, plus the nursery `RunHost` runs in: a host module starts
background tasks there (the player's scene load and remount) and cancels
them itself; tasks still running when the engine completes are cancelled.
The event loop traits move with it unchanged. `RunHost` performs, in
order:

1. create the Platform and load the backend;
2. create command queues, then the `AsyncEngine` with the startup plan;
3. inside `co::Run`: activate and run Platform, Graphics and Engine, call
   `register_modules` (the input system is registered first and stored in
   the context), stop the engine when the last window closes, await engine
   completion;
4. tear down: engine stop, platform stop, engine reset, graphics stop,
   backend unload, platform reset — the order every example uses today;
5. convert exceptions to `EXIT_FAILURE` with one log line, as today, and
   still tear down whatever was created.

`RunHost` lives in a library, so it reaches the backend loader through
`GraphicsBackendLoader::GetInstanceRelaxed()`, as the editor runner does; the
strict singleton accepts its first call only from the executable.

The shared entry point (logging setup, type registry, the call to the
application's `MainImpl`) is the static library `oxygen::host-main`
(`Host/Main.cpp`); the player and every example link it.

Threading: everything in `Oxygen.Host` runs on the engine thread. Remote
requests arrive there too: the control server's coroutines run on the
platform's `io_context`, which the host loop polls on the engine thread, and
commands run at frame start through the dispatcher. Events may be published
from any thread into the thread-safe event log.

`src/Oxygen/EditorInterface/EngineRunner.cpp` keeps its own copy: the editor
host is not changed ([§10](#10-owner-decisions)).

### 3.2 `HostWindow`

`AppWindow` with a constructor taking platform, engine, graphics and an
optional framebuffer size instead of `DemoAppContext`. It is no longer a
composition component: the host module owns it through a `shared_ptr`, which
its lifecycle coroutine needs. Behaviour is unchanged
(lifecycle coroutine for close, resize and terminate; per-frame
framebuffers; deferred release; binding the ImGui module to the window on
attach and unbinding it on cleanup, with the `ImGuiLayer` registration that
`AppWindow` performs since I1) except one fix: the framebuffers
are colour-only. Runtime
composition and the ImGui overlay write colour only; the editor dropped the
same depth texture from its composite targets (ED-M08 M08.V0 R2).

### 3.3 `ViewPublisher`

The publish/composite half of `DemoModuleBase`, owned by the host module:

- `BeginFrame(FrameContext&)`: register the window surface, apply a pending
  resize, drop the surface when the window is gone (`OnFrameStartCommon`).
- `Publish(FrameContext&, std::span<const CompositionView>,
std::optional<ViewId> main)`: per scene view, normalize viewport and
  scissor, resolve the camera content rectangle, ensure the scene and
  composite framebuffers, publish; release views no longer present
  (`OnPublishViews`, `ReleaseInactiveRuntimeViews`). The span is borrowed
  for the call only.
- `Compose(extra_layers)`: z-ordered layers onto the window framebuffer,
  then the caller's texture layers, `RegisterRuntimeComposition`
  (`OnCompositing`).
- `GetOrCreateViewId(name)`, per publisher instead of a process static.

The `OnRuntimeMainViewReady` call to DemoShell becomes a main-view handler
that fires for the view marked as main; `HostModule` routes it to a virtual
that `DemoModuleBase` overrides. The `ClearBackbufferReferences` hook is
dropped: every example implements it as a no-op.

### 3.4 `SceneStage`

`SceneControlBlock` and `ActiveScene` together, with one change: `Stage()`
calls `CollectMutationsStart()` on the new scene and `PublishStaged()` calls
`CollectMutationsEnd()`, so both ends live in one class. Scenes already
collect mutations for their own observers; the start call opens the replay
window, so an observer that registers once the scene is published still
receives the mutations made while the scene was built. A scene set directly
with `SetScene()` replays nothing. `Clear()` drops the
active and staged scenes (used at session end). Usage stays: stage and
build during scene mutation; publish at frame start before
`FrameContext::SetScene`. The atomics and mutex stay: UI code reads the
active scene from other threads today.

### 3.5 `HostModule`

An abstract `EngineModule` that owns a `HostWindow`, a `ViewPublisher` and a
`SceneStage`, and runs them in `kFrameStart`, `kSceneMutation`,
`kPublishViews` and `kCompositing`. A subclass supplies its views and marks
the main game view by id, so a view that is built conditionally cannot
shift the mark:

```cpp
struct HostViews {
  std::vector<vortex::CompositionView> views;
  std::optional<ViewId> main; // the main game view, when there is one
};
virtual auto BuildViews(FrameContext&, HostViews&) -> void = 0;
```

At frame start the host module registers the surface, publishes a staged
scene and sets the frame's scene. Its other hooks have defaults:
`OnHostAttached` (after the window exists), `BuildWindowProperties`,
`OnScenePublished`, `OnMainViewReady`, `AppendCompositionLayers` and
`RetainInactiveView`. Attach failures and exceptions are logged and fail the
attach.

The host applies the toolbar's main-view variables
([§5.2](#52-main-view-console-variables)) to that view only. `DemoModuleBase`
derives from `HostModule` and keeps DemoShell ownership, the UI-test hooks
and its default window title, and saves the console window placement in the
demo settings file. The `host.*` toolbar variables are archived console
variables: each application keeps them in its own archive, and a value set on
the command line wins over the saved one. `RunDemo` builds an example's command line from
`RuntimeCli` and its own option groups, then calls `RunHost`.

### 3.6 `RuntimeCli`

The option groups from `DemoCli.h` and the frame-capture headers, bound to
one `RuntimeCliState`. `MakeRuntimeOptions` takes the subset of runtime
options an application offers. `ApplyRuntimeCli` validates the values,
writes them into `HostConfig` and seeds `ngin.target_fps` and `gfx.vsync`
when those options were passed; every invalid or contradictory value throws
`RuntimeCliError`. The player uses the graphics-tooling and capture groups
([§7](#7-oxygenplayer-composition)); examples keep the runtime group.

## 4. Pause, frame step and play sessions

### 4.1 How other engines do it

Engines whose editor UI is not drawn by the engine keep the game state
apart from the edited state and throw it away on Stop:

- **O3DE** (Qt editor): Play in Editor starts a game entity context from the
  editor's entities; Stop "shuts down the game context and re-activates the
  editor context", with begin/end notifications for both transitions
  ([EditorEntityContextRequests](https://docs.o3de.org/docs/api/frameworks/aztoolsframework/class_az_tools_framework_1_1_editor_entity_context_requests),
  [EditorEntityContextNotification](https://docs.o3de.org/docs/api/frameworks/aztoolsframework/class_az_tools_framework_1_1_editor_entity_context_notification)).
- **CRYENGINE** (Sandbox, MFC/Qt): Ctrl+G switches to an in-editor game
  mode, while the Launcher runs the "pure game" as a separate executable
  ([Sandbox interface](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048817),
  [Game Executable Launcher](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535321)).
- **Unity**: entering Play mode reloads the scene and, optionally, the
  scripting domain to reset script state; skipping the domain reload keeps
  static state between runs and is documented as the speed-for-accuracy
  trade
  ([Configurable Enter Play Mode](https://docs.unity3d.com/6000.6/Documentation/Manual/configurable-enter-play-mode.html)).
- **Unreal** (engine-drawn editor, for contrast): Play in Editor duplicates
  the editor world into a separate PIE world and destroys it on end
  ([StartPlayInEditorSession](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UEditorEngine/StartPlayInEditorSession)).

Common rule: a play session owns a separate runtime instance of the scene
plus every runtime registration it made; Stop destroys all of it and
notifies the systems that took part. Oxygen already has the separate
instance (the player instantiates from the cooked scene, never from the
authored one); what is missing is the session boundary.

Pause and step in the same engines:

- **Unreal**: `UGameplayStatics::SetGamePaused` pauses the world; tick
  functions opt in with `FTickFunction::bTickEvenWhenPaused`
  (`AActor::SetTickableWhenPaused`) and input bindings with
  `bExecuteWhenPaused`, so pause menus run on the paused world. The PIE
  "Frame Skip" button advances one frame of the paused world
  (`UWorld::bDebugFrameStepExecution`).
- **Godot**: `SceneTree.paused` stops processing; each node's
  `process_mode` (`PROCESS_MODE_ALWAYS`, `WHEN_PAUSED`, `PAUSABLE`,
  `INHERIT`) decides whether it keeps running.
- **Unity**: `Time.timeScale = 0` stops `FixedUpdate` and zeroes
  `deltaTime`; `Update` keeps running with `unscaledDeltaTime`, so UI
  code runs by default. `EditorApplication.Step` advances one frame.

Every one of them lets selected gameplay code run while paused. Oxygen's
rule in [§4.3](#43-pause-and-step-semantics) therefore has a module-level
opt-in from the start.

### 4.2 Engine API

On `IAsyncEngine`, implemented by `AsyncEngine`:

```cpp
// Pause and step: a paused engine keeps rendering; simulation stops.
auto Pause() -> void;
auto Resume() -> void;
[[nodiscard]] auto IsPaused() const -> bool;
//! Pauses if running, then simulates `frames` frames and stays paused.
auto Step(uint32_t frames) -> void; // ngin.step defaults to 1
//! Steps requested and not yet started; 0 once all began or were cancelled.
[[nodiscard]] auto GetPendingSteps() const -> uint32_t;

// Play sessions.
//! Requests a session; returns false if one exists or is pending.
auto BeginSession() -> bool;
//! Ends the running session at the next frame start.
auto EndSession() -> void;
[[nodiscard]] auto GetSessionState() const -> SessionState; // kNone,
                                                            // kRunning,
                                                            // kPaused
```

Every call is a request, safe from any thread like `Stop()`
(`Engine/AsyncEngine.h:83`), stored under a mutex and applied once per frame
at the start of `PhaseFrameStart`: before `TimeManager::BeginFrame`
integrates the frame's time and before any module's `OnFrameStart`. A frame
is therefore entirely paused or entirely running, and `GetSessionState()`
and `IsPaused()` report applied state, never a pending request. `Resume`
cancels remaining stepped frames. `EndSession` with no session is a no-op;
`EndSession` before a requested session began withdraws the request.

The commands `ngin.pause`, `ngin.resume`, `ngin.step [frames]` and
`ngin.session.end` are registered by the command dispatcher, which replies
once their effect is known
([commands and remote control §3.4](commands-and-remote-control.md#34-dispatch-and-jobs)).
Starting a session needs content, so `ngin.session.begin` is registered by
the host that knows what to instantiate (the player).

### 4.3 Pause and step semantics

Pausing is a phase rule in the frame loop, not a per-module convention.

| Phase                                                          | Paused frame                                  | Why                                                                       |
| -------------------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------- |
| `kFrameStart`                                                  | runs                                          | fences, latched variables, session transitions, scene publish             |
| `kInput`                                                       | runs                                          | held keys stay current; ImGui, toolbar and console input; pause menus     |
| `kNetworkReconciliation`                                       | runs                                          | applying packets is not simulation (no network module exists today)       |
| `kFixedSimulation`                                             | skipped                                       | the clock yields zero steps already (`SimulationClock.cpp:93-97`)         |
| `kGameplay`                                                    | skipped, except modules that tick when paused | the simulation is what pause stops                                        |
| `kSceneMutation`, `kTransformPropagation`                      | run                                           | the host stages and publishes scenes; tooling edits need world transforms |
| `kPublishViews` … `kPresent`                                   | run                                           | a paused game still renders                                               |
| `kAsyncPoll`, `kBudgetAdapt`, `kFrameEnd`, `kDetachedServices` | run                                           | loader batches, hot reload, release queues                                |

- The simulation clock is paused, so the game delta is zero and the
  interpolation alpha is zero. `ModuleTimingData::is_paused` and
  `time_scale` are filled from the clock at `AsyncEngine.cpp:701-712`, so
  `FrameContext::IsGamePaused()` is true and the `oxygen.time.is_paused()`
  binding (`Bindings/Packs/Core/TimeBindings.cpp:115`) reports it.
- **Tick when paused.** `EngineModule` gains
  `virtual auto TicksWhenPaused() const noexcept -> bool { return false; }`.
  A module that returns true still gets `kGameplay` while paused, with a
  zero game delta and the real physical delta; it owns the consequences.
  No module returns true in this document's scope. `ScriptingModule` will
  return true once scene scripts carry a `runs_when_paused` slot flag
  (authoring data: scene schema, cooked descriptor and editor Play section;
  owner decision), and will then dispatch `on_update` and action events only
  to those slots. Until then scripts need nothing: their hooks and input
  action events run in `kGameplay` (`ScriptingModule.cpp:609-618`), which
  does not run.
- **Input edges.** Per-frame edges reset at every frame start
  (`InputSystem.cpp:128-133`), so an edge produced while paused is gone
  before anyone resumes; no code is added for that. Triggered edges latched
  for fixed-step consumers (`Action.h:176-186`) stay latched and are
  consumed by the first fixed step after `Resume` or `Step`; that is the
  useful behaviour when stepping (press jump, step, see the jump). A key
  pressed during pause and still held at resume is held, not newly
  triggered. Trigger windows (hold, tap) run on physical time by design
  (`InputSystem.cpp:139-150`) and keep running while paused.
- **Step.** `Step(n)` simulates `n` consecutive frames, then the engine is
  paused again. On a stepped frame `TimeManager::BeginFrame`
  (`Engine/TimeManager.cpp:52-62`) calls a new
  `SimulationClock::StepFixed()` instead of `Advance(physical)`: it sets the
  accumulator to exactly one fixed timestep, lets `ExecuteFixedSteps` run
  one step despite the pause, and reports that step as `DeltaTime()`. So
  `kFixedSimulation` runs once, `kGameplay` gets a game delta equal to the
  fixed timestep, the alpha is zero, and `time_scale` is ignored. A step
  is independent of how long the user waited and of the frame rate.
  Stepping while running pauses first. A pause remainder in the
  accumulator is discarded by `StepFixed()`.
- **Variable-timestep code on a stepped frame** sees the fixed timestep as
  its delta; that is the only delta that makes a step reproducible.
  Physical delta stays the real frame interval, which is small because
  frames keep running while paused: at a capped 60 fps it is about one
  fixed step; uncapped it is shorter. Hold and tap windows therefore
  progress by wall time per step, not by game time. Documented, not fixed.
- Pausing without a session is allowed; DemoShell examples use it the same
  way. MultiView's exposure proofs pause through `engine->Pause()` and
  `Resume()`.

Rejected: letting each module check `IsGamePaused()`. Every module would
need the same code, and one forgotten check breaks pause.

Rejected: zero-delta frames as "pause" (today's accidental behaviour).
Scripts still react to input, so the game is not paused.

Rejected: no opt-in at all. A shipped game needs a pause menu, and every
engine in [§4.1](#41-how-other-engines-do-it) exposes one. The opt-in is
one virtual with a false default; the per-slot flag that would use it is
the expensive part and is deferred.

### 4.4 Session lifecycle

A session is the time between an applied `BeginSession` and an applied
`EndSession`. Two hooks on `EngineModule`:

```cpp
//! Prepare for a session; false aborts the session before any module
//! instantiates content.
virtual auto OnSessionBegin(observer_ptr<IAsyncEngine>) -> bool
{ return true; }
//! Release everything this module created or changed during the session.
//! The frame's scene is still alive and still published.
virtual auto OnSessionEnd(observer_ptr<IAsyncEngine>) -> void { }
```

Each module owns its own cleanup; the engine only sequences. Process exit
does not run session hooks: `OnShutdown` already covers it (the scripting
module's shutdown at `ScriptingModule.cpp:426-450` is the model for both).

**Begin**, applied in `kFrameStart`:

1. The engine resets the simulation clock to zero, unpaused, and calls
   `OnSessionBegin` on every module in priority order. If one returns
   false, the engine calls `OnSessionEnd` on the modules that already
   began, in reverse order, logs the module name and leaves the state at
   `kNone`. The host sees this through its own hook (it begins last) or
   through `GetSessionState()` next frame.
2. Modules record their pre-session state. Input, which runs first
   (`kModulePriorityHighest`, `InputSystem.h:56`), records the actions and
   mapping contexts present and which contexts are active.
3. The host's own `OnSessionBegin` starts its content: the player stages a
   scene, launches `Instantiate` as a task in a nursery it owns, and
   publishes when the task completes
   ([§7](#7-oxygenplayer-composition)). Instantiation failure (missing or
   unloadable scene, no camera) is reported (`scene_failed`) and the host
   calls `EndSession()` itself, so the session ends and Play is available
   again.

**End**, requested any time; applied in `kFrameStart` before any module's
`OnFrameStart`:

1. The engine calls `OnSessionEnd` on every module in reverse priority
   order. The frame's scene is alive and published throughout, so every
   module can still reach its nodes:
   - **Host module**: cancels the instantiation task through its nursery
     if it is still running
     ([P-3](#p-3-ending-a-session-during-instantiation)), drops any
     residency pins it holds, and notes that the stage must be
     cleared. It keeps the main view id, its view-state handle and the
     window-sized framebuffers: they do not depend on the scene, and
     recreating them each session buys nothing. It calls
     `Renderer::NotifyViewDiscontinuity` (`Vortex/Renderer.h:619-621`) on
     the main view's handle so exposure restarts as on a cold start.
   - **Scripting**: `on_destroy` on every instance while the bindings and
     the scene are reachable, then `DestroyRuntime()` and
     `CreateRuntime()` ([P-2](#p-2-recreating-the-lua-state)). No global
     survives into the next session; bytecode stays in the compilation
     service caches, so the next session does not recompile.
   - **Input**: removes every action and context that was not present at
     begin, restores the recorded active state of the rest, clears pending
     deactivations and latched edges. This covers the starting
     contexts, any context a script added, and any change the host made
     during the session: everything added after begin is session-scoped
     by definition. A host that wants a context to outlive sessions adds
     it before `BeginSession`. (UE scopes Enhanced Input contexts the same
     way: they live on the local player's subsystem, which dies with the
     PIE world.)
   - **Physics**: there is no physics engine module today
     (`Physics/System/IWorldApi.h:44-46`). Whichever module creates a
     world in `OnSessionBegin` destroys it in `OnSessionEnd`.
   - **Renderer**: nothing; per-scene view resources go with the views the
     host unpublishes.
2. The engine resets pause to off and the simulation clock to zero, and
   reports the session as ended.
3. Module `OnFrameStart` runs. The host's clears the stage
   (`SceneStage::Clear()`) and sets the frame's scene to what it wants
   shown after a session: the player shows none; a future in-process
   editor mode shows the edited scene again. The scripting module sees the
   scene change in later phases with no instances left to end.

The asset loader cache is not cleared: the next session reuses loaded
assets. Recooked content is picked up only when the host remounts
([P-4](#p-4-recooked-content-in-a-warm-player)).

**Room for an in-process editor game mode.** The session API does not
assume who owns the pre- and post-session scene, which is what such a mode
needs. What it would still require, and this design does not provide, is a
cooked source for the runtime scene (an in-memory cook or a cook to disk)
and a scripting module that attaches instances only inside a session, so
publishing the edited scene with script slots does not start them. Neither
changes the hooks above.

### 4.5 Who holds control after Stop

- **Player standalone**: shows "Stopped"; Play begins a new session.
- **Player launched by the editor**: emits `session_ended` and keeps running.
  The editor may run `ngin.session.begin` to begin a new session, or
  `ngin.quit` to end the process. This replaces relaunching the player for every Play; the
  editor-side change belongs to the
  [script authoring LLD](../../../../design/editor/lld/script-authoring.md#8-play)
  and is listed in [P-5](#p-5-editor-flow-with-a-warm-player).
- **Later, an in-process editor game mode**: the same engine API serves it;
  the edited scene is never the session's scene.

## 5. Runtime debug UI

### 5.1 Rule

Toolbar controls are shortcuts to console variables and commands: the
control shows the current value, so a change typed in the console or set by
a startup plan appears in the toolbar at once, and everything the toolbar
does can be typed. DemoShell panels follow the same
[rule](commands-and-remote-control.md#2-the-rule): each control runs a
command or sets a console variable; the settings service still persists the
values.

### 5.2 Main-view console variables

Vortex owns the view-mode vocabulary because it owns the modes
([diagnostics service](../vortex/lld/diagnostics-service.md) makes debug
modes a renderer-owned vocabulary):

```cpp
struct ViewModeInfo {
  std::string_view canonical_name; // console value, e.g. "lit"
  std::string_view display_name;   // "Lit"
  std::string_view description;    // tooltip, may be empty
  ShaderDebugModeFamily family;    // kNone for the common modes
  std::optional<RenderMode> render_mode;             // none for "lit"
  std::optional<ShaderDebugMode> shader_debug_mode;  // none for "lit"
  bool supported;
  std::string_view unsupported_reason;
};
OXGN_VRTX_NDAPI auto EnumerateViewModes() noexcept
  -> std::span<const ViewModeInfo>;
OXGN_VRTX_NDAPI auto FindViewModeInfo(std::string_view) noexcept
  -> const ViewModeInfo*;
OXGN_VRTX_NDAPI auto FindViewModeInfo(RenderMode, ShaderDebugMode) noexcept
  -> const ViewModeInfo*;
```

Common modes, matching the editor's table: `lit`, `unlit` (base colour),
`wireframe`, `lit-wireframe`, `direct-lighting`, `indirect-lighting`,
`world-normals`, `roughness`, `metalness`, `linear-depth`, `shadow-mask`.
Names use the shader debug registry's hyphenated style. Every other
`ShaderDebugModeRegistry` entry follows grouped by family; unsupported
entries are listed with their reason. `host.view_mode` accepts only
supported names: the console rejects any other value through the
variable's allowed values (`CVarDefinition::allowed_values`).

Only the main game view gets the full scene render and these controls.
Other views (picture-in-picture, maps) are configured by the code that
creates them; MultiView, the multi-view showcase, sets up its own views.
So the variables are registered by `Oxygen.Host` and applied by
`HostModule` to the view its subclass marks as main. This is the
viewport-client model: in UE, view mode and show flags belong to the
viewport that shows the view, while `ShowFlag.*` console variables are
global overrides for all views; Oxygen's `host.*` variables are the former
and `vtx.*` variables the latter.

| Variable                 | Type, default | Effect on the main view when false (bools) or set (string)                                    |
| ------------------------ | ------------- | --------------------------------------------------------------------------------------------- |
| `host.view_mode`         | string, `lit` | Sets `render_settings` from the catalog entry; `lit` resets both to none. Unknown names fail. |
| `host.show.shadows`      | bool, true    | Clears `kShadows`                                                                             |
| `host.show.environment`  | bool, true    | Clears `kEnvironment`                                                                         |
| `host.show.translucency` | bool, true    | Clears `kTranslucency`                                                                        |
| `host.show.volumetrics`  | bool, true    | Clears `kVolumetrics`                                                                         |
| `host.show.grid`         | bool, true    | Clears `kGroundGrid`                                                                          |
| `host.show.atmosphere`   | bool, true    | Forces `with_atmosphere` off                                                                  |
| `host.show.height_fog`   | bool, true    | Forces `with_height_fog` off                                                                  |
| `host.show.local_fog`    | bool, true    | Forces `with_local_fog` off                                                                   |

There is no `host.show.lighting`: Vortex's `kSceneLighting` bit gates the
whole scene pass (depth prepass, resolve and scene-texture publication),
which only reduced profiles turn off, so clearing it leaves the view
without scene colour. Turning lighting off is the `unlit` view mode.

A `host.show.*` variable only removes a feature the view asked for. The
subclass decides what the main view asks for: VortexBasic sets
`with_atmosphere`, `with_height_fog` and `with_local_fog` from its command
line, and the player derives them from the scene's environment systems,
the same check the editor makes for local fog (`EditorModule.cpp:120-136`).
`HostModule` does not derive them itself, so an application that turns a
feature off keeps it off. The host edits the view's `feature_mask`, never
its `feature_profile`. `MainViewOptions` (`Host/MainViewOptions.h`) holds
the variable names and the application rule.

The show menu also lists the existing renderer switches
`vtx.occlusion.enable` and `vtx.shadows.contact.enable`
(`Renderer.cpp:186-188`) under Advanced.

**One owner for the main view's mode.** DemoShell's Diagnostics panel no
longer sets the renderer-global render or debug mode; `host.view_mode` is the
only control, and it is archived with the other `host.*` variables. A
view override wins over the global mode (`Renderer.cpp:2864`), so at `lit`,
which carries no override, the renderer-global mode applies. Nothing in the
examples sets it; if an application does, the toolbar labels the
control with the effective global mode and marks it "(global)" rather than
claiming "Lit".

### 5.3 `Oxygen.ImGui` widgets

**ImGui module.** `oxygen::imgui::ImGuiModule`, an engine module at
`kImGuiModulePriority`, owns what the former `Vortex/Internal/ImGuiRuntime` owned
except rendering: it creates the graphics backend through
`Graphics::CreateImGuiGraphicsBackend()`, binds the SDL platform backend to a
window (`SetWindowId`), applies the Spectrum style, fonts and icons, calls
`NewFrame` at `kFrameStart` and `EndFrame` at `kFrameEnd`, and exposes
`GetImGuiContext()` and `IsFrameActive()`. Apps keep drawing in
`OnGuiUpdate`; they get the context from the module instead of the renderer
(DemoShell `DemoModuleBase`, `DemoShellUi`, LightBench, MultiView).

**`ImGuiLayer`** is the Host adapter between the module and Vortex: a
`vortex::SurfaceLayerSource` whose `IsActive` is the module's frame state and
whose `Record` calls the graphics backend's `Render`. `RunHost` registers it
for the window's surface
([multi-view composition §12.4](../vortex/lld/multi-view-composition.md#124-surface-layer-sources)).
It lives in Host because it needs both Vortex and ImGui; `Oxygen.ImGui` stays
free of Vortex. Until Host exists, DemoShell registers the same adapter.

Widgets are generic, with no knowledge of Vortex or the console:

The widgets live in `Oxygen/ImGui/Widgets/` (`Toolbar.h`: `DropdownMenu`,
`ToolbarButton`, `PlayControls`; `StatsOverlay.h`) and
`Oxygen/ImGui/Console/ConsoleHost.h`. DemoShell already uses `StatsOverlay`
and `ConsoleHost`; its own copies are gone.

| Widget          | Shape                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DropdownMenu`  | The UE-style button plus popup: search field, section headers, check items (show flags) or radio items (view modes) with icon and shortcut text, submenus. Takes items with label, icon, shortcut, state and an action.                                                                                                                                                                                                                                           |
| `ToolbarButton` | Icon button with tooltip and an optional dropdown arrow.                                                                                                                                                                                                                                                                                                                                                                                                          |
| `PlayControls`  | Play, Pause, Step and Stop as one centred group; enables each from the session state.                                                                                                                                                                                                                                                                                                                                                                             |
| `StatsOverlay`  | A translucent panel at the top-right, below a given margin: the frame sequence number (always, so a screenshot names its frame), FPS and frame time, engine CPU and pacing time, the time of every engine phase that does work (slowest three highlighted) and CPU time against budget. Values are averaged over about half a second; the columns are sized for every row the enabled sections can show, so the panel keeps its width. Takes its config directly. |
| `GpuWorkChart`  | One stacked horizontal bar of a total split into coloured segments with a budget marker, a legend in the bar's fixed order (ms, %, max), expandable rows with up to four sub-items, and a sparkline. Takes its own row type.                                                                                                                                                                                                                                      |
| `ConsoleHost`   | The console panel and the command palette with their hotkeys (`` ` ``, Ctrl+Shift+P), extracted from `DemoShellUi.cpp:154-225`. Window placement is held in memory; DemoShell restores and saves it itself as today.                                                                                                                                                                                                                                              |

### 5.4 Viewport toolbar

`host::ViewportToolbar` binds the widgets to the console and the Vortex
catalog and draws one strip across the top of the window:

```text
[ ◐ Lit ▾ ] [ 👁 ▾ ] [ ⏱ ]       [ ▶ ] [ ❚❚ ] [ ▶| ] [ ■ ]       [ >_ ]
  view mode  show    stats          play controls                 console
```

| Control   | Action                                                                                                                                               |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| View mode | `DropdownMenu` of `EnumerateViewModes()`; choosing runs `host.view_mode <name>`. Shortcuts as in UE: Alt+4 lit, Alt+3 unlit, Alt+2 wireframe.        |
| Show      | `DropdownMenu` of the `host.show.*` variables; a click toggles one; "Use defaults" resets them.                                                      |
| Stats     | `DropdownMenu` with the stats overlay sections (`host.ui.stats.fps`, `.engine`, `.phases`, `.budget`) and the GPU work overlay (`host.ui.gpu_work`). |
| Play      | No session: `ngin.session.begin`. Paused: `ngin.resume`.                                                                                             |
| Pause     | `ngin.pause`                                                                                                                                         |
| Step      | `ngin.step` (enabled while paused)                                                                                                                   |
| Stop      | `ngin.session.end`                                                                                                                                   |
| Console   | Toggles the console window.                                                                                                                          |

`HostModule` draws the toolbar, the stats overlay and `ConsoleHost` at
`kGuiUpdate` unless headless. Play controls appear only when the host registers `ngin.session.begin`.
F1 hides or shows the toolbar and stats overlay (`host.ui.visible`); it
starts visible. The console and the command palette keep their own hotkeys
and F1 never hides them. The toolbar reads session and pause state from
`GetSessionState()` and `IsPaused()` each frame; it needs no callback.

### 5.5 GPU work overlay

`host::GpuWorkOverlay` shows the renderer's
[GPU work summary](../vortex/lld/gpu-work-summary.md) with `GpuWorkChart`. It
replaces the unread `rndr.gpu_timestamps.viewer` variable.

| Variable           | Type, default | Effect                                                                                          |
| ------------------ | ------------- | ----------------------------------------------------------------------------------------------- |
| `host.ui.gpu_work` | bool, false   | Shows the overlay and enables the GPU timeline while shown; F1 hides it with the stats overlay. |

Each frame it reads `DiagnosticsService::GetLatestGpuWorkSummary()` and
rebuilds rows only when `revision` changes. Rows come from the summary in
catalog order, labelled and coloured by `EnumerateGpuWorkBuckets()`:

- the whole-frame GPU bar: the main view's groups, then each further view,
  shared work and GPU idle (grey) as one segment each, with the budget marker
  at the engine's GPU budget;
- one row per further view, collapsed by default;
- shared work, done once per frame for all views (composition, IBL, uploads),
  as its own row labelled "Shared";
- the `frame_gpu` sparkline, and the valid and invalid frame counts.

With no summary it shows "Waiting for GPU timing", or the reason the
renderer lacks `kDiagnosticsAndProfiling`. The overlay holds a GPU timeline
lease ([GPU work summary §4.3](../vortex/lld/gpu-work-summary.md#43-service-api))
only while it is visible, so a hidden overlay costs nothing and never
overrides another requester.

## 6. Console and command palette in the player

Mandatory. `ConsoleHost` gives the player the console window and the
command palette (quick command bar): backtick opens the console,
Ctrl+Shift+P the palette. Both work in every session state, with no
session, and with the toolbar hidden.

## 7. `Oxygen.Player` composition

| Part                                                                                                                                                                                                                                                                                                                                        | Size estimate |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------- |
| Launch file read and schema validation (embedded schema, `oxygen_embed_json_schemas`)                                                                                                                                                                                                                                                       | ~150 lines    |
| Events: lifecycle, script diagnostics through `ScriptingModule::AddDiagnosticHandler` and `ToJson`, and GPU work while subscribed, published to the host's event log; commands and lifecycle values: [scene doc](scene-instantiation-and-player.md#control-and-events)                                                                      | ~300 lines    |
| `PlayerModule : HostModule`: mounts in launch order; registers `ngin.session.begin`; on session begin loads the scene by `AssetKey::FromVirtualPath`, instantiates through `SceneStage` and `oxygen::instantiation::Instantiate` in its own nursery; one main view through `Scene::ActiveCamera()`; toolbar; failure and "Stopped" messages | ~500 lines    |
| `main`: the command line in the [scene doc](scene-instantiation-and-player.md#command-line), then `RunHost`                                                                                                                                                                                                                                 | ~80 lines     |

The player begins a session as soon as the engine runs. Modules: input,
scene observer sync, scripting, the player module and the Vortex renderer
with the capabilities RenderScene uses (including
`kDiagnosticsAndProfiling`, which the GPU timeline behind the GPU work
overlay and the `gpu_work` event requires). No physics module.

Diagnostic handlers and the log callback are registered once at attach and
survive sessions; the Lua state does not ([§4.4](#44-session-lifecycle)).

Not in the player: DemoShell panels, free or orbit cameras, environment or
post-process editing, scene picker, persisted settings, log or problems
windows (the editor shows those from the control server's events; standalone, the log
goes to the console output), UI tests (its ImGui use is the toolbar and the
console, which are tested in `Oxygen.ImGui` and `Oxygen.Host`).

**Gaps for a shipped game player.** Not V0.1 work, listed so they are not
mistaken for covered:

| Gap                                                           | Where it would land                                                 |
| ------------------------------------------------------------- | ------------------------------------------------------------------- |
| Fullscreen, borderless and monitor selection; DPI awareness   | launch file `window`, `HostWindow`                                  |
| Device removed or reset during a session                      | Vortex's view failure path already reports it; the session must end |
| Crash dumps and a crash reporter                              | `RunHost` (Aftermath exists for GPU faults only)                    |
| Exit codes with meaning (bad launch file, scene failed, user) | `RunHost` return value                                              |
| Audio and a pause menu driven by scripts                      | `TicksWhenPaused()` plus the `runs_when_paused` slot flag           |

## 8. Migration

| Consumer                                                                                       | Change                                                                                                                                                                                                                                                                                                                                                                                                            | Removed                                                                                                                                                                                                                           |
| ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| VortexBasic                                                                                    | Uses `RunHost`, `HostModule`, the toolbar; no DemoShell link                                                                                                                                                                                                                                                                                                                                                      | its own surface and resize code (`VortexBasic/MainModule.cpp:535-570`), its use of `AppWindow` and `DemoAppContext`                                                                                                               |
| DemoShell                                                                                      | `DemoModuleBase : HostModule`; `SceneStage` replaces `SceneControlBlock`/`ActiveScene`; the toolbar and `ConsoleHost` replace its console wiring; `StatsOverlay` comes from `Oxygen.ImGui`; `ImGuiLayer` and `GpuWorkOverlay` come from Host; the camera rig ticks in `kInput` on the physical delta so it keeps flying while paused; panel controls run commands; `RunDemo` replaces each example's startup code | `AppWindow`, `SceneControlBlock`, `ActiveScene`, the generic half of `DemoModuleBase`, `StatsOverlay`, its interim `ImGuiLayer` and `GpuWorkOverlay`, the generic fields of `DemoAppContext`. Panels, services and settings stay. |
| Other examples (Async, InputSystem, LightBench, MultiView, Physics, RenderScene, TexturedCube) | `main_impl.cpp` calls `RunHost`; scene publishing uses `SceneStage`; each marks its main view; MultiView pauses through `engine->Pause()`                                                                                                                                                                                                                                                                         | the copied bootstrap and teardown, about 150 lines each                                                                                                                                                                           |
| Editor interop                                                                                 | none from the host; the ImGui seam (I1) removes its `renderer.enable_imgui` mapping                                                                                                                                                                                                                                                                                                                               | `renderer.enable_imgui` in `EngineRunner.cpp` and `Config.h`                                                                                                                                                                      |

## 9. Delivery order and exit gates

| Step | Delivers                                                                                                                      | Gate                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ---- | ----------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| I1   | ImGui module in `Oxygen.ImGui`; Vortex surface layer sources; `ImGuiLayer` (in DemoShell until H1); ImGui call sites migrated | Vortex has no `imgui`, `Oxygen.ImGui` or `Platform/ImGui` link or include; `Oxygen.Vortex.LinkTest` and the installed-SDK consumer build pass; every example that drew ImGui before draws the same UI; console hotkeys and ImGui input capture behave as before; a headless run creates no ImGui module                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| H1   | `Oxygen.Host`: `RunHost`, `HostWindow`, `ViewPublisher`, `SceneStage`, `HostModule`, `RuntimeCli`                             | Unit tests for `SceneStage` (stage, publish, clear, mutation collection, generation) and `RuntimeCli` parsing; the library builds with no example dependency                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| H2   | Engine pause, step and sessions; module session hooks; scripting and input session cleanup                                    | Engine tests with fake modules: while paused, `kFixedSimulation` and `kGameplay` run only for a module whose `TicksWhenPaused()` is true, and it sees a zero game delta; `IsGamePaused()` and `time_scale` follow the clock; `Step(1)` from paused runs `kFixedSimulation` exactly once and `kGameplay` once with the fixed timestep, independent of wall time, and leaves the engine paused; `Step` from running pauses first; `Resume` cancels pending steps; a request made mid-frame applies at the next frame start, never inside a frame; session end calls hooks in reverse order before any `OnFrameStart`, and a fake module asserts the frame's scene is non-null inside its hook; a module whose `OnSessionBegin` fails leaves `kNone` and the earlier modules get `OnSessionEnd`. Scripting: a global set in session one is nil in session two; `on_destroy` runs at session end with scene nodes reachable; diagnostic handlers still fire in session two. Input: contexts and actions added during a session are gone after it, pre-session contexts return to their recorded active state, a key held across resume reports held and not triggered |
| H3   | Vortex view-mode catalog; `Oxygen.ImGui` widgets; `host::ViewportToolbar` and the `host.*` variables                          | Catalog covers every supported debug mode and no unsupported one is selectable; `host.show.*` only removes features and only from the main view (MultiView's other views are byte-identical with and without the variable); `host.view_mode lit` leaves no override on the view; the toolbar reflects a value set from the console and labels a renderer-global mode "(global)"; `host.ui.gpu_work` shows the GPU work overlay, which holds a timeline lease only while visible and lists every bucket the summary publishes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| P1   | `Oxygen.Player`                                                                                                               | SIP-3 to SIP-5 of the [player design](scene-instantiation-and-player.md#5-exit-gates), plus: console and command palette work in every state; toolbar switches view mode and show flags; Pause stops scripts; Step advances one fixed step; Stop then Play runs the scene again from its start state with no leftover script global, input context or view; `end_session` while instantiation is in flight (ASan build, slow loader) leaves no staged scene, no session contexts, and every late loader callback completes without touching freed memory; ten Play/Stop cycles leave the counts of Lua states, actions, mapping contexts, published views and mounted sources equal to the counts after the first; `scene_failed` is followed by `session_ended` and the next `play` works; `play` after a recook shows the new content                                                                                                                                                                                                                                                                                                                           |
| E1   | VortexBasic on `Oxygen.Host`, without DemoShell                                                                               | Renders as before                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| R1   | Commands and remote control ([design](commands-and-remote-control.md))                                                        | RC-1 to RC-7 of the [design](commands-and-remote-control.md#9-exit-gates)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| E2   | DemoShell on `Oxygen.Host`                                                                                                    | RenderScene loads every maintained scene as before (SIP-2); settings persist as before; console and toolbar work; DemoShell panels change state only through commands or console variables ([rule](commands-and-remote-control.md#2-the-rule)); the free camera moves while the engine is paused                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| E3   | The other examples on `RunHost` and `SceneStage`                                                                              | Each starts, renders and shuts down cleanly; MultiView's extra views are unaffected by the toolbar; MultiView's exposure proofs pass through `engine->Pause()`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |

## 10. Owner decisions

Recorded 2026-10-11; review position added 2026-10-12. The decision stands
until the owner changes it.

| Question                                        | Decision                                                                                                                                      | Review                                                                                                                                                                                |
| ----------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Physics hydration in the player                 | No: the editor has no physics sidecar authoring in V0.1.                                                                                      | Confirmed. The world-owning module still uses the session hooks ([§4.4](#44-session-lifecycle)).                                                                                      |
| Free or orbit debug camera in the player        | No: the player is a game player and shows the scene's active camera.                                                                          | Confirmed; matches every shipped-game launcher.                                                                                                                                       |
| Exposure and post-process editing in the player | No: the editor and the scene are the player's only sources.                                                                                   | Confirmed.                                                                                                                                                                            |
| Editor interop adopting the host or publisher   | No. A GitHub issue only if a concrete benefit appears.                                                                                        | Confirmed; the interop's runner has a different event loop and lifetime (`EngineRunner.cpp:43-269`).                                                                                  |
| Order                                           | `Oxygen.Host` → Player → one demo without DemoShell → DemoShell → the other examples.                                                         | Confirmed.                                                                                                                                                                            |
| UI                                              | Minimal, shared between player and DemoShell: UE-style view-mode and show-flag menus as console shortcuts; Play/Pause/Stop at the top centre. | Confirmed, with Step beside Pause.                                                                                                                                                    |
| Console in the player                           | Mandatory: console window and command palette.                                                                                                | Confirmed.                                                                                                                                                                            |
| Pause and frame step                            | Built into `AsyncEngine`, with a simple API, plus `SimulationClock::StepFixed()` and `EngineModule::TicksWhenPaused()` (2026-10-12).          | Confirmed, plus `EngineModule::TicksWhenPaused()` and `SimulationClock::StepFixed()` ([§4.3](#43-pause-and-step-semantics)). Both are small; the first is needed by any shipped game. |
| Stop without exiting                            | Built into the engine; returns control to the editor. The editor keeps one player running across Play and Stop (2026-10-12).                  | Confirmed. Needs the editor flow change in [P-5](#p-5-editor-flow-with-a-warm-player).                                                                                                |
| Remount on editor `play`                        | Always (2026-10-12).                                                                                                                          | As recommended in [P-4](#p-4-recooked-content-in-a-warm-player).                                                                                                                      |
| View mode and show flags                        | Main game view only. Other views are set up by their owner; MultiView sets up its own.                                                        | Confirmed; the host owns them as viewport-client state ([§5.2](#52-main-view-console-variables)).                                                                                     |
| UI tests in the player                          | None.                                                                                                                                         | Confirmed; the widgets are tested where they live.                                                                                                                                    |
| DemoShell settings                              | Unchanged, except the Diagnostics panel's render mode, which goes through `host.view_mode` in E2 (2026-10-12).                                | Done in E2: the panel's render and debug mode are gone and `host.view_mode` is archived ([§5.2](#52-main-view-console-variables)).                                                    |

## 11. Open problems for review

Each problem has an answer. Where the answer is the owner's, it says so.

### P-1 Session hooks change the module contract

`OnSessionBegin`/`OnSessionEnd` are new virtuals on `EngineModule`
(`Core/EngineModule.h`), the contract every module implements, with
defaults that keep every existing module working.

Answer: virtuals, like every other lifecycle step on the contract
(`EngineModule.h:108-122`). A subscription (`SubscribeModuleAttached`,
`AsyncEngine.h:156`) suits observers that are not modules; every session
participant here is a module, and the toolbar polls `GetSessionState()`.
O3DE's EBus notifications and UE's `FEditorDelegates::BeginPIE`/`EndPIE`
exist because their participants are not all engine modules. Add a
subscription only when a non-module consumer appears. `OnSessionBegin`
returns `bool` so a module can refuse ([§4.4](#44-session-lifecycle)).

### P-2 Recreating the Lua state

Known: the Lua state is created in `ScriptingModule::OnAttached` (`:387`)
and closed in `OnShutdown` (`:426-450`); bindings are registered on
creation; the bytecode cache belongs to the compilation service
(`ScriptCompilationService.h:37-41`), not the state.

Answer: factor `:387-…` into `CreateRuntime()` and `:426-450` into
`DestroyRuntime()`; `OnSessionEnd` calls both. `DestroyRuntime()` must
reset everything the shutdown path resets today plus the per-state members
that shutdown leaves for the destructor: `lua_state_`, `global_env_ref_`,
`runtime_env_ref_`, `slot_runtimes_`, `active_frame_slots_`,
`active_slot_indices_`, the task queue, the input event bridge's queued
events, the source-reload file map and `observed_scene_`. Everything that
must survive is module- or engine-owned and keyed by nothing in the state:
`engine_`, the diagnostic handlers, the source-changed handler, the
hot-reload service and the compilation caches.

No binding pack keeps process-wide state: the active engine and frame
context are stored per `lua_State` (`LuaBindingCommon.h:124-134`), the
events runtime is a userdata in the registry
(`EventsBindings.cpp:835-849`), and `src/Oxygen/Scripting` has no
file-scope mutable statics. `ScriptTaskQueue::EndSession`
(`ScriptingModule.h:99`) already uses the word "session" for a shutdown
drain; rename it `Drain()` so the two do not collide.

### P-3 Ending a session during instantiation

Known: `Instantiate` is a coroutine that awaits asset loads across frames
through a shared pending block (`SceneInstantiation.cpp:286-311`). If Stop
arrives while it runs, the scene it is building must not be published.

Answer: cancel, do not count generations. The host runs `Instantiate` as a
task in a nursery it owns; `OnSessionEnd` cancels the nursery. OxCo
cancels a coroutine at its suspension point, so the loader callback that
fires later finds only the shared pending block, which it owns by
`shared_ptr`, and triggers an event nobody awaits. The staged scene is
dropped by `SceneStage::Clear()`; the starting contexts that were already
added are removed by the input restore because they carry the session
generation; scripts attached to a staged scene never became instances,
because the scripting module observes only the published scene. The
generation counter proposed earlier would duplicate what cancellation
already guarantees. Gate: the ASan run in P1.

### P-4 Recooked content in a warm player

Known: after Stop the mounts stay. If the editor recooks and plays again,
the player must see the new cooked files. The loader can prepare a
replacement mount set off-thread, wait for in-flight reads and commit it
atomically, detaching the old sources' cache entries
(`IAssetLoader.h:382-403`).

Answer: on `ngin.session.begin`, the player always remounts the
launch mounts between sessions with `PrepareLooseCookedRootsAsync`,
`WaitForPendingLoadsAsync` and `CommitPreparedMounts`, with no session
running and no instantiation in flight. The toolbar's Play runs the same
command, so it remounts too
([commands and remote control §7](commands-and-remote-control.md#7-oxygenplayer)).
Cost: the swap detaches the old cache entries, so the
next session reloads assets from disk, which for V0.1 scenes is small next
to instantiation. Alternative: a `remount` flag the editor sets only after
a cook that changed outputs; it saves that reload and adds a stale-content
failure whenever the editor's change detection is wrong. Owner decision 2026-10-12: always remount.

### P-5 Editor flow with a warm player

The [script authoring LLD](../../../../design/editor/lld/script-authoring.md#8-play)
launches a new player for every Play and ends it on Stop. With sessions,
Stop runs `ngin.session.end` and Play runs `ngin.session.begin` in the running player,
launching only when none runs. The owner approved this on 2026-10-12 and the
LLD carries the edits below.

Answer: adopt the warm player; it removes a process launch, mounts and
pipeline warm-up from every Play. The LLD edits, once approved:

- "Play button": Restart runs `ngin.session.end` then
  `ngin.session.begin`; Stop runs `ngin.session.end`; the dropdown gains
  "Close player" (runs `ngin.quit`).
- "Flow" step 4: write the launch file and start the player only when
  none is connected; otherwise run `ngin.session.begin`.
- "Flow" step 5: clear Output and Problems on `session_started`; drive
  the toolbar from `session_started`, `paused`, `resumed` and
  `session_ended`.
- "Flow" step 6: Stop runs `ngin.session.end`; closing the project or the
  editor runs `ngin.quit` and waits briefly before ending the process; a
  closed connection means the player died, and the next Play launches a new
  one.
- Session settings (window size, VSync, frame cap) apply to the next
  launch only; a change while a player runs shows "applies after Close
  player".

## 12. Not in scope

- Editor interop and `EditorInterface` changes, beyond removing
  `renderer.enable_imgui` in I1. Editor consumption of the GPU work summary
  is tracked separately ([GPU work summary §2](../vortex/lld/gpu-work-summary.md#2-boundaries-and-consumers)).
- DemoShell's panels, services and settings, beyond the migration in
  [§8](#8-migration).
- Physics in the player.
- An in-process editor game mode; the session API in [§4](#4-pause-frame-step-and-play-sessions)
  leaves room for it, and [§4.4](#44-session-lifecycle) names what it
  would still need.
- The `runs_when_paused` script slot flag and the audio module that would
  use `TicksWhenPaused()`.
