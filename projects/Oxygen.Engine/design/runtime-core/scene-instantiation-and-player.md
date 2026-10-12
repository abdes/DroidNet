# Scene instantiation and the player host

Status: **design; approved by owner decision SD-2 on 2026-10-11; the player
built 2026-10-13 (`src/Oxygen/Player`)**. Needed by the editor's
Play command ([script authoring](../../../../design/editor/lld/script-authoring.md#8-play))
and by [scene script hosting](../scripting/scene-script-hosting.md#5-scene-script-attachment).

Summary: turning a cooked `SceneAsset` into a running scene is today the
example shell's job (DemoShell `SceneLoaderService`, about 3,400 lines). It
moves into an engine library, `Oxygen.SceneInstantiation`, which the examples
and a new engine executable, `Oxygen.Player`, both use. The player runs one
cooked scene through its active camera with scripting and input, the way a
shipped game would, and reports to whoever launched it.

## 1. Why

- A runtime that ships with the SDK cannot depend on example code.
- DemoShell mixes instantiation with showcase policy: it picks the first
  camera and may create one or rewrite its aspect and clipping, and it
  restores persisted demo settings. Play must show the scene exactly as
  authored.
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
renderables with material overrides, cameras, lights, local fog volumes and
the environment; instantiates the model scenes the scene instances under
their instance roots ([scene composition](scene-composition.md)); applies the
scene's start state (below); and then attaches scripts. It applies authored values only: no camera is invented and
no camera value is rewritten. Unsupported or failed records become issues
with the node index; the rest of the scene loads.

### Scene start state

Games choose their camera in the scene, not in the launcher: Unity renders
the scene's cameras and exposes the MainCamera-tagged one as `Camera.main`,
Godot uses the camera marked `current` (else the first camera added), and
Unreal uses the player's view target. Input works the same way: Unreal
adds Enhanced Input mapping contexts when the player starts and Unity's
`PlayerInput` names a default action map, rather than activating every
context that happens to be loaded. Oxygen follows the same model for both.

- **Authored starting camera.** The scene asset records one camera node as
  its starting camera (`SceneAssetDesc::starting_camera`, a node index or
  none; the scene format version increases). The scene descriptor carries it
  as `starting_camera`. The editor authors it in the scene's Play section,
  not on cameras: which camera starts is a property of the scene.
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
- The scene's starting contexts are the only start mechanism. The
  per-context `auto_load` and `auto_activate` flags are removed from the
  input schema, the cooked mapping context descriptor and every loader;
  RenderScene and DemoShell start the contexts of the scene they
  instantiate, like the player.

Physics hydration stays in DemoShell, running on `SceneInstance::nodes`,
until physics authoring is scheduled.

## 3. `Oxygen.Player`

An SDK executable (`bin/Oxygen.Player.exe`) with a window, Vortex, input and
scripting. It is built on `Oxygen.Host`, and its debug UI (console, command
palette, viewport toolbar, Play/Pause/Stop) and its pause and stop
behaviour are specified in [runtime host and debug UI](runtime-host.md).

### Launch

`Oxygen.Player.exe --launch <file>`; the file validates against
`oxygen.player-launch.schema.json`:

| Field                 | Meaning                                                                                                 |
| --------------------- | ------------------------------------------------------------------------------------------------------- |
| `version`             | `1`.                                                                                                    |
| `mounts`              | Ordered cooked roots or PAK files (a `.pak` file), highest precedence last.                             |
| `scene`               | Scene virtual path, e.g. `/.cooked/Scenes/Level.oscene`.                                                |
| `script_source_roots` | Roots for external script sources; enables source reload when non-empty.                                |
| `window`              | Title, client size, VSync and frame cap (`0` is off, else frames per second); all optional.             |
| `diagnostics_pipe`    | Optional name of the launcher's named pipe (without the `\\.\pipe\` prefix) for the event stream below. |

Relative paths are relative to the launch file's folder. A launch file that
breaks the schema, or names a mount or source root that does not exist,
ends the process with an error naming the file, the value and the rule.

No setting persists between launches; the launch file is the whole input:
the player neither loads nor saves archived console variables or console
history (`EngineConfig::persist_console_state`).

### Command line

`Oxygen.Player.exe -l <launch file> [options]`. The options are for runs
outside the editor and override the launch file:

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
| graphics tooling, capture | `--debug-layer`, `--aftermath` and the frame capture options of `RuntimeCli`.                     |

`-v <level>` sets the log verbosity. `Examples/Content/play_scene` writes a
launch file for a cooked example scene and runs the player with it.

### Behavior

- Mounts the roots in order, loads the scene, instantiates it with scripts,
  and renders through the scene's active camera every frame, so a script
  that moves or switches the camera moves the view. Aspect follows the window
  according to the camera's Auto/Fixed policy.
- A scene without an active camera, or a scene that fails to load, shows a
  message in the window and reports a lifecycle failure; the process stays up
  so the user can read it.
- Input reaches the scene's mapping contexts. Closing the window, a `stop`
  request on the pipe, or `oxygen.app.request_stop()` ends the process. The
  toolbar's Stop and an `end_session` request end only the play session
  ([runtime host](runtime-host.md#4-pause-frame-step-and-play-sessions)).

### Event stream

The launcher creates the pipe and the player connects to it at start. One
JSON object per line, validated by `oxygen.player-events.schema.json`:

| Event        | Payload                                                                                                                                                                  |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `lifecycle`  | `starting`, `session_started`, `scene_loaded`, `scene_failed` (with issues), `paused`, `resumed`, `session_ended`, `stopped`. `stopped` means the process is ending.     |
| `log`        | Severity (`fatal` to `info`), channel and text: `oxygen.log` output on channel `script`, engine lines on the logging source file's name                                  |
| `diagnostic` | A [script diagnostic](../scripting/script-diagnostics.md#2-model)                                                                                                        |
| `gpu_work`   | A [GPU work summary](../vortex/lld/gpu-work-summary.md#41-types), sent while the `stats` request has it on and only when its `revision` changes (at most twice a second) |

The launcher sends one request object per line on the same pipe, validated
by `oxygen.player-requests.schema.json`; an invalid request is logged and
ignored:

| Request       | Effect                                                                                                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `play`        | Remounts the launch mounts and begins a session on `scene` (a scene virtual path; the launch file's scene when omitted); ignored while one runs ([runtime host](runtime-host.md#44-session-lifecycle)). |
| `pause`       | Pauses the engine.                                                                                                                                                                                      |
| `resume`      | Resumes it.                                                                                                                                                                                             |
| `step`        | Simulates `frames` frames (default 1) and stays paused.                                                                                                                                                 |
| `end_session` | Ends the session; the process stays up and reports `session_ended`.                                                                                                                                     |
| `stop`        | Ends the process.                                                                                                                                                                                       |
| `stats`       | `gpu_work: true` holds a GPU timeline lease and starts `gpu_work` events; `false` releases it. Off at launch; survives sessions.                                                                        |

A `scene_failed` is followed by `session_ended`, so the launcher can send
`play` again; a `play` whose remount fails reports both too. The reader runs
on its own thread and hands requests to the engine thread; nothing on the
pipe thread touches the engine. When the launcher closes the pipe, the
player stops.

Script diagnostics come from the scripting module's handlers (hook errors,
source reload) and from instantiation: attach problems are sent after
`scene_loaded`, and a script whose compilation fails later is reported when
it fails (`SceneScriptAttachment`'s late diagnostic handler).

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

| ID    | Check                                                                                                                                                                                                                           |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SIP-1 | Instantiation tests for each record family, the starting camera and its first-camera fallback, the starting input contexts and their priorities, a missing asset and an unsupported record.                                     |
| SIP-2 | RenderScene loads every maintained scene with the same result as before the move.                                                                                                                                               |
| SIP-3 | The player loads a launch file, applies its window settings, renders through the starting camera with the starting input contexts active, follows a script's camera and context switches, and streams lifecycle and log events. |
| SIP-4 | A failing script in the player streams its diagnostic; a source edit reloads it while running.                                                                                                                                  |
| SIP-5 | `end_session` ends the session and leaves the process ready for `play`; `stop` ends the process cleanly; a bad launch file fails with a schema diagnostic.                                                                      |
| SIP-6 | `stats` with `gpu_work: true` produces schema-valid `gpu_work` events, one per summary revision; `false` stops them and releases the timeline lease; no `gpu_work` event is sent before the first `stats` request.              |
