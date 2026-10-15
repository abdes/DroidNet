# Scene instantiation and the player host

Status: **design; approved by owner decision SD-2 on 2026-10-11; the player
built 2026-10-13 (`src/Oxygen/Player`)**. Needed by the editor's Play command
([script authoring](../../../../design/editor/lld/script-authoring.md#8-play))
and by
[scene script hosting](../scripting/scene-script-hosting.md#5-scene-script-attachment).

Summary: turning a cooked `SceneAsset` into a running scene is today the example
shell's job (DemoShell `SceneLoaderService`, about 3,400 lines). It moves into
an engine library, `Oxygen.SceneInstantiation`, which the examples and a new
engine executable, `Oxygen.Player`, both use. The player runs one cooked scene
through its active camera with scripting and input, the way a shipped game
would, and reports to whoever launched it.

## 1. Why

- A runtime that ships with the SDK cannot depend on example code.
- DemoShell mixes instantiation with showcase policy: it picks the first camera
  and may create one or rewrite its aspect and clipping, and it restores
  persisted demo settings. Play must show the scene exactly as authored.
- Script attachment must live in the engine
  ([hosting §5](../scripting/scene-script-hosting.md#5-scene-script-attachment));
  its caller must too.

## 2. `Oxygen.SceneInstantiation`

Depends on Data, Content, Input, Scene and Scripting; links no renderer.

```cpp
struct SceneInstantiationIssue {
  data::AssetKey scene;               // the scene or an instanced one
  std::optional<uint32_t> node_index; // when the record belongs to a node
  std::string message;
};

struct SceneInstantiationRequest {
  std::shared_ptr<const data::SceneAsset> asset;
  observer_ptr<scene::Scene> target;
  observer_ptr<const scripting::SceneScriptAttachment> scripts; // null: none
  observer_ptr<engine::InputSystem> input; // null: no starting contexts
};

struct SceneInstance {
  std::vector<scene::SceneNode> nodes; // index = cooked node index
  std::vector<SceneInstantiationIssue> issues;
  std::vector<scripting::SceneScriptSlotOutcome> scripts;
};

auto Instantiate(SceneInstantiationRequest, IAssetLoader&)
  -> co::Co<SceneInstance>;
```

Instanced scenes and starting contexts are references, not dependencies, so
instantiation loads them through `IAssetLoader::StartLoadScene` and
`StartLoadInputMappingContextAsset`. Instanced scenes contribute nodes and
scripts, never start state or environment.

It creates nodes in index order with names, flags and transforms; attaches
renderables with material overrides, cameras, lights, local fog volumes and the
environment; instantiates the model scenes the scene instances under their
instance roots ([scene composition](scene-composition.md)); applies the scene's
start state (below); and then attaches scripts. It applies authored values only:
no camera is invented and no camera value is rewritten. Unsupported or failed
records become issues with the node index; the rest of the scene loads.

### Scene start state

Games choose their camera in the scene, not in the launcher: Unity renders the
scene's cameras and exposes the MainCamera-tagged one as `Camera.main`, Godot
uses the camera marked `current` (else the first camera added), and Unreal uses
the player's view target. Input works the same way: Unreal adds Enhanced Input
mapping contexts when the player starts and Unity's `PlayerInput` names a
default action map, rather than activating every context that happens to be
loaded. Oxygen follows the same model for both.

- **Authored starting camera.** The scene asset records one camera node as its
  starting camera (`SceneAssetDesc::starting_camera`, a node index or none; the
  scene format version increases). The scene descriptor carries it as
  `starting_camera`. The editor authors it in the scene's Play section, not on
  cameras: which camera starts is a property of the scene.
- **Runtime active camera.** `scene::Scene` owns an active camera node:
  `SetActiveCamera(SceneNode)` accepts a live node with a camera component;
  `ActiveCamera()` returns it, or nothing once that node is destroyed or loses
  its camera. Instantiation sets it to the starting camera, or, when none is
  authored, to the first camera in node order with an issue saying so.
- **Scripts switch cameras** with `oxygen.scene.active_camera()` and
  `oxygen.scene.set_active_camera(node)`; setting a node without a camera
  returns `false` and changes nothing.
- **Starting input contexts.** The scene asset records an ordered list of
  mapping context keys, each with a priority (a descriptor-local table; the
  scene descriptor carries it as `starting_input_contexts`). Instantiation
  hydrates each context, adds it to the input system with its priority and
  activates it. A context that fails to load is an issue; the others still
  activate. The editor authors the list in the scene's Play section.
- **Scripts switch input contexts** with
  `oxygen.input.activate_context(context)` and
  `oxygen.input.deactivate_context(context)`, where `context` is a mapping
  context asset reference (an `asset` parameter of kind
  `input_mapping_context`). A context not yet added is hydrated on first
  activation with its asset's default priority.
- The scene's starting contexts are the only start mechanism. The per-context
  `auto_load` and `auto_activate` flags are removed from the input schema, the
  cooked mapping context descriptor and every loader; RenderScene and DemoShell
  start the contexts of the scene they instantiate, like the player.

Physics hydration stays in DemoShell, running on `SceneInstance::nodes`, until
physics authoring is scheduled.

## 3. `Oxygen.Player`

An SDK executable (`bin/Oxygen.Player.exe`) with a window, Vortex, input and
scripting. It is built on `Oxygen.Host`, and its debug UI (console, command
palette, viewport toolbar, Play/Pause/Stop) and its pause and stop behaviour are
specified in [runtime host and debug UI](runtime-host.md).

### Launch

`Oxygen.Player.exe --launch <file>`; the file validates against
`oxygen.player-launch.schema.json`:

| Field                 | Meaning                                                                                                                                                                 |
| --------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `version`             | `2`.                                                                                                                                                                    |
| `mounts`              | Ordered cooked roots or PAK files (a `.pak` file), highest precedence last.                                                                                             |
| `scene`               | Scene virtual path, e.g. `/.cooked/Scenes/Level.oscene`.                                                                                                                |
| `script_source_roots` | Roots for external script sources; enables source reload when non-empty.                                                                                                |
| `window`              | Title, `mode` (`windowed` or `borderless`), framebuffer size in physical pixels while windowed, VSync and frame cap (`0` is off, else frames per second); all optional. |
| `control`             | Optional; starts the control server below: `port` (`0`, the default, takes an ephemeral one) and `exit_when_owner_leaves` (default `false`).                            |

Relative paths are relative to the launch file's folder. A launch file that
breaks the schema, or names a mount or source root that does not exist, ends the
process with an error naming the file, the value and the rule.

No setting persists between launches; the launch file is the whole input: the
player neither loads nor saves archived console variables or console history
(`EngineConfig::load_console_state` and `save_console_state` are both off).
Every Play reads the launch file again and takes its `mounts` and `scene`, so a
launcher that publishes content to new roots rewrites the file before Play.
`control` and `script_source_roots` apply at launch only. `window` sets the
window a launch opens with; a launcher changes a running player's display mode,
window size, VSync and frame rate limit with `host.window.mode`,
`host.window.size`, `gfx.vsync` and `ngin.target_fps`. A borderless window
covers its display at the desktop resolution; its size applies once it is
windowed again.

### Command line

`Oxygen.Player.exe -l <launch file> [options]`. The options are for runs outside
the editor and override the launch file:

| Option                    | Effect                                                                                            |
| ------------------------- | ------------------------------------------------------------------------------------------------- |
| `-l`, `--launch <file>`   | The launch file; required.                                                                        |
| `-w`, `--resolution WxH`  | Client size.                                                                                      |
| `-F`, `--fullscreen`      | Full screen.                                                                                      |
| `-s`, `--vsync <bool>`    | VSync.                                                                                            |
| `-r`, `--fps <n>`         | Frame cap; `0` is off.                                                                            |
| `-f`, `--frames <n>`      | Stop after `n` frames.                                                                            |
| `-c`, `--cvar name=value` | Set a console variable at startup, parsed for its type; repeatable. An unknown name is a warning. |
| `--verify-hashes`         | Verify cooked content hashes when mounting.                                                       |
| `--control [port]`        | Start the control server on a loopback port; `0`, the default, takes an ephemeral one.            |
| graphics tooling, capture | `--debug-layer`, `--aftermath` and the frame capture options of `RuntimeCli`.                     |

`-v <level>` sets the log verbosity. `Examples/Content/play_scene` writes a
launch file for a cooked example scene and runs the player with it.

### Behavior

- Mounts the roots in order, loads the scene, instantiates it with scripts, and
  renders through the scene's active camera every frame, so a script that moves
  or switches the camera moves the view. Aspect follows the window according to
  the camera's Auto/Fixed policy.
- A scene without an active camera, or a scene that fails to load, shows a
  message in the window and reports a lifecycle failure; the process stays up so
  the user can read it.
- Input reaches the scene's mapping contexts. Closing the window, `ngin.quit`,
  or `oxygen.app.request_stop()` ends the process. The toolbar's Stop and
  `ngin.session.end` end only the play session
  ([runtime host](runtime-host.md#4-pause-frame-step-and-play-sessions)).

### Control and events

With `control` in the launch file, or `--control`, the player starts the
[control server](commands-and-remote-control.md#4-control-server): a launcher
reads the discovery file of the process it started, connects with `rpc.hello`
(as owner, when it set `exit_when_owner_leaves`) and runs commands as JSON-RPC
methods:

| Command                      | Effect                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ngin.session.begin [scene]` | Reads the launch file again, remounts its loose cooked roots and begins a session on `scene` (a scene virtual path; the launch file's scene when omitted); refused while one runs ([runtime host](runtime-host.md#44-session-lifecycle)). Unchanged PAK mounts are reused; changed PAK mounts fail the Play, since only a new process mounts them. Replies with the load outcome: the scene and the camera it is viewed through, or the failure. |
| `ngin.pause`, `ngin.resume`  | Pause or resume the engine.                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `ngin.step [frames]`         | Simulates `frames` frames (default 1) and stays paused; replies once they have run.                                                                                                                                                                                                                                                                                                                                                              |
| `ngin.session.end`           | Ends the session; the process stays up. Replies once it has ended.                                                                                                                                                                                                                                                                                                                                                                               |
| `ngin.quit`                  | Ends the process.                                                                                                                                                                                                                                                                                                                                                                                                                                |

The toolbar's Play, Pause, Step and Stop run the same commands. The player
publishes these topics to the event log; their data is validated by
`oxygen.player-events.schema.json`, and `log` and `command` events come from the
control server:

| Topic        | Data                                                                                                                                                                    |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `lifecycle`  | `session_started`, `scene_loaded`, `scene_failed` (with issues), `paused`, `resumed`, `session_ended`, `stopping`. `stopping` means the process is ending.              |
| `diagnostic` | A [script diagnostic](../scripting/script-diagnostics.md#2-model)                                                                                                       |
| `gpu_work`   | A [GPU work summary](../vortex/lld/gpu-work-summary.md#41-types), only when its `revision` changes (at most twice a second), while some client subscribes to `gpu_work` |
| `stats`      | `fps`, whole frames per second from the engine's sampled rate, when it changes (at most twice a second), while some client subscribes to `stats`                        |

A `scene_failed` is followed by `session_ended`, so the launcher can play again;
a play whose remount fails reports both too. While a client subscribes to
`gpu_work` the player holds a GPU timeline lease, released when the last
subscriber leaves; the lease survives sessions. When the owner disconnects and
`exit_when_owner_leaves` is set, the player stops.

Script diagnostics come from the scripting module's handlers (hook errors,
source reload) and from instantiation: attach problems are published after
`scene_loaded`, and a script whose compilation fails later is reported when it
fails (`SceneScriptAttachment`'s late diagnostic handler).

The `gpu_work` payload mirrors `GpuWorkSummary` field for field, with buckets
written as their catalog ids (`"base_pass"`), never enum values, and
`schema_version` from `GpuWorkSummary::kSchemaVersion`. The player module
serializes it on the engine thread when it sees a new `revision`; Vortex does
not know the event format.

## 4. Migration

DemoShell's scene loading calls `Instantiate` and keeps only showcase policy
(scene picker, fallback camera, persisted settings, physics hydration).
RenderScene behavior is otherwise unchanged.

## 5. Exit gates

| ID    | Check                                                                                                                                                                                                                                       |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SIP-1 | Instantiation tests for each record family, the starting camera and its first-camera fallback, the starting input contexts and their priorities, a missing asset and an unsupported record.                                                 |
| SIP-2 | RenderScene loads every maintained scene with the same result as before the move.                                                                                                                                                           |
| SIP-3 | The player loads a launch file, applies its window settings, renders through the starting camera with the starting input contexts active, follows a script's camera and context switches, and publishes lifecycle and log events.           |
| SIP-4 | A failing script in the player publishes its diagnostic; a source edit reloads it while running.                                                                                                                                            |
| SIP-5 | `ngin.session.end` ends the session and leaves the process ready for `ngin.session.begin`; `ngin.quit` ends the process cleanly; a bad launch file fails with a schema diagnostic.                                                          |
| SIP-6 | Subscribing to `gpu_work` produces schema-valid `gpu_work` events, one per summary revision; the last unsubscribe or disconnect stops them and releases the timeline lease; no `gpu_work` event is published before the first subscription. |
