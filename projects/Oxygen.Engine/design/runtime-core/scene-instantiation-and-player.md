# Scene instantiation and the player host

Status: **design; approved by owner decision SD-2 on 2026-10-11**. Needed by the editor's
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

Depends on Data, Content, Scene and Scripting; links no renderer.

```cpp
struct SceneInstantiationRequest {
  std::shared_ptr<const data::SceneAsset> asset;
  observer_ptr<scene::Scene> target;
  observer_ptr<scripting::SceneScriptAttachment> scripts; // null: no scripts
};

struct SceneInstance {
  std::vector<scene::SceneNode> nodes; // index = cooked node index
  std::vector<SceneInstantiationIssue> issues;
};

auto Instantiate(const SceneInstantiationRequest&, IAssetLoader&)
  -> co::Co<SceneInstance>;
```

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
scripting.

### Launch

`Oxygen.Player.exe --launch <file>`; the file validates against
`oxygen.player-launch.schema.json`:

| Field                 | Meaning                                                                  |
| --------------------- | ------------------------------------------------------------------------ |
| `mounts`              | Ordered cooked roots or PAK files, highest precedence last.              |
| `scene`               | Scene virtual path.                                                      |
| `script_source_roots` | Roots for external script sources; enables source reload when non-empty. |
| `window`              | Title, client size, VSync and frame cap (off or frames per second).      |
| `diagnostics_pipe`    | Optional named pipe for the event stream below.                          |

No setting persists between launches; the launch file is the whole input.

### Behavior

- Mounts the roots in order, loads the scene, instantiates it with scripts,
  and renders through the scene's active camera every frame, so a script
  that moves or switches the camera moves the view. Aspect follows the window
  according to the camera's Auto/Fixed policy.
- A scene without an active camera, or a scene that fails to load, shows a
  message in the window and reports a lifecycle failure; the process stays up
  so the user can read it.
- Input reaches the scene's mapping contexts. Closing the window, a `stop`
  request on the pipe, or `oxygen.app.request_stop()` ends the session.

### Event stream

One JSON object per line, validated by `oxygen.player-events.schema.json`:

| Event        | Payload                                                             |
| ------------ | ------------------------------------------------------------------- |
| `lifecycle`  | `starting`, `scene_loaded`, `scene_failed` (with issues), `stopped` |
| `log`        | Severity, channel and text, including `oxygen.log` output           |
| `diagnostic` | A [script diagnostic](../scripting/script-diagnostics.md#2-model)   |

The launcher sends `{"request":"stop"}` on the same pipe.

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
| SIP-5 | `stop` ends the session cleanly; a bad launch file fails with a schema diagnostic.                                                                                                                                              |
