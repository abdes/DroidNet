# Scene script hosting

Status: **design; owner decisions SD-1–SD-7 taken on 2026-10-11**.
Parameters follow the
[parameter declaration decision](script-parameter-declarations.md) (option B). Delivery
is scheduled by the
[ED-M08 plan](../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md#m0810--engine-capability-parity-in-the-editor).

Summary: script slots become a complete engine feature. A script declares its
parameters ([§3](#3-script-model)), receives an instance `self` and
start/destroy hooks, and runs in its authored execution order. The engine,
not the example shell, attaches cooked slots to a scene
([§5](#5-scene-script-attachment)), recompiles changed sources and swaps them
into running slots ([§6](#6-source-reload)), and reports every failure as a
structured [script diagnostic](script-diagnostics.md). Physics bindings are
outside this design.

## 1. Baseline and gaps

| Fact today                                                                                                                                                        | Consequence                                                                                 |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| No parameter declaration; `ScriptLoader` gives assets empty defaults (`Content/Loaders/ScriptLoader.h`).                                                          | Tools cannot list a script's parameters; scripts repeat defaults.                           |
| `ScriptParamType` has no asset or node reference.                                                                                                                 | GUID strings escape dependency closure, packaging and retargeting.                          |
| Slot chunks return `on_gameplay`/`on_scene_mutation`; no start or destroy hook (`ScriptingModule.cpp`, `RebuildSlotRuntime`).                                     | Scripts guard one-time setup with flags (`rotate.lua`, `orbit-shapes.lua`); listeners leak. |
| `ScriptSlotRecord::execution_order` is cooked but never read; `IAssetLoader::HydratedScriptSlot` drops it.                                                        | Slot order is accidental.                                                                   |
| Slot attachment, source resolution and compilation live in DemoShell (`SceneLoaderService::AttachScripting`, `QueueSlotCompilation`).                             | Every host must copy example code.                                                          |
| Content reload notifies only assets with embedded bytecode (`Content/Internal/ScriptHotReloadService.cpp`); the cooker forbids compiling external-source scripts. | Edited source files never reach running slots.                                              |
| The reload subscriber mutates a shared executable through `const_cast` (`ScriptingModule.cpp`, `UpdateBytecode`).                                                 | Replaced by the slot's ordinary executable change (§6).                                     |
| Compile errors are strings; runtime errors are log lines.                                                                                                         | Replaced by [script diagnostics](script-diagnostics.md).                                    |

## 2. Ownership

| Owner               | Responsibility                                                                                                                   |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Data`       | Cooked records: declarations, parameter values, slots ([§4](#4-parameter-types-and-cooked-records)).                             |
| `Oxygen.Scripting`  | Analyzer (in `Oxygen.Scripting.Analysis`, §7), compiler, diagnostics, instance model and hooks, scene attachment, source reload. |
| `Oxygen.Cooker`     | Script asset import (declarations), sidecar validation against declarations, reference dependencies.                             |
| `Oxygen.Scene`      | `ScriptingComponent` slots with execution order and compile state; unchanged ownership.                                          |
| Scene instantiation | Creates nodes from a `SceneAsset` and calls scene attachment ([player host](../runtime-core/scene-instantiation-and-player.md)). |

## 3. Script model

### 3.1 Declaration

A script is a chunk that returns the table created by `oxygen.script`, as in
the [declaration decision](script-parameter-declarations.md#4-recommended-declaration).
`oxygen.script` records the declaration on the table and returns it. A chunk
that returns anything else is a load diagnostic for the slot.

### 3.2 Instances

Each slot is one instance. The chunk is loaded into the slot's private
environment (unchanged), and the engine creates the instance table `self`,
whose metatable indexes the script table, so methods resolve on the script.

| Field         | Value                                                                                       |
| ------------- | ------------------------------------------------------------------------------------------- |
| `self.node`   | The owning `SceneNode` userdata.                                                            |
| `self.params` | Read-only table of effective values: the slot override if valid, else the declared default. |

`asset` values are asset references accepted by every `oxygen.assets` load
function and by `oxygen.input.on_action`. `node` values are `SceneNode`
userdata, or `nil` when unset or destroyed. Other fields on `self` belong to
the script.

`scene.current_node(ctx)`, `scene.param(ctx, key)` and the legacy `ctx`
helpers (`GetParam`, `SetLocalRotationEuler`, `SetLocalRotation`,
`GetDeltaSeconds`) are removed; `self` replaces them.

### 3.3 Hooks

All hooks are optional methods on the script table.

| Hook                            | Called                                                                                                                                                                         |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `on_start(self)`                | Once, in the first Gameplay phase after the slot is ready, before any `on_gameplay` of that phase.                                                                             |
| `on_fixed_simulation(self, dt)` | Each fixed step.                                                                                                                                                               |
| `on_gameplay(self, dt)`         | Each frame.                                                                                                                                                                    |
| `on_scene_mutation(self, dt)`   | Each frame, in the scene mutation phase.                                                                                                                                       |
| `on_destroy(self)`              | Once, when an instance whose `on_start` completed ends: slot removed, node destroyed, scene unloaded, source reloaded or module shut down. `self.node` may no longer be alive. |

Within a phase, instances run in ascending `execution_order`, then scene
node order, then slot position. Order is deterministic across runs.

Event and input connections made from an instance's hooks belong to that
instance. They are disconnected after its `on_destroy`, so a reload or a
removed slot never leaves a listener behind.

An error in a hook faults the instance (existing policy: it stops until its
executable changes) and publishes a runtime diagnostic. A faulted instance
gets no further hooks; `on_destroy` runs only if `on_start` completed.

A reload ends the old instance (`on_destroy`) and starts a new one
(`on_start`). Instance state is not carried over: that keeps reload
predictable, and scripts that need persistence keep it on nodes or in
parameters.

## 4. Parameter types and cooked records

`ScriptParamType` gains `kAsset` and `kNode`.

| Type     | Value storage (`ScriptParamRecord::value`)         | Notes                                                                       |
| -------- | -------------------------------------------------- | --------------------------------------------------------------------------- |
| `kAsset` | `AssetKey` (16 bytes) and the declared `AssetType` | Resolved by the cooker from a virtual path; recorded as a scene dependency. |
| `kNode`  | Scene-local node index (`SceneNodeIndexT`)         | Resolved at attachment to the node created for that index.                  |

Script assets gain a descriptor-local declaration array after `ScriptAssetDesc`,
located by `ScriptAssetDesc::params`: one `ScriptParamRecord` per parameter,
holding its key, type and default value; a `kAsset` value carries the declared
asset type with a nil key. Asset and node parameters declare no default, and a
parameter without an authored default cooks its type's zero value.
`kScriptAssetVersion` increases to 3; maintained content is recooked. Authoring
hints (`min`, `max`, `step`, `unit`, `description`, `options`, `color`) are not
cooked; tools read them from source through the analyzer (§7).

`ScriptSlotRecord::execution_order` is honored, and
`IAssetLoader::HydratedScriptSlot` carries it.

Sidecar parameters accept `"type": "asset"` with a virtual path value and
`"type": "node"` with a node index; a declared `int` is written `"int32"`. The
sidecar pipeline validates every override against the script's declarations: an
unknown key, a type mismatch, an out-of-range node index or an asset of the
wrong kind is an error naming the JSON path, so a cooked scene never carries an
override the script does not declare.

## 5. Scene script attachment

`scripting::SceneScriptAttachment` replaces DemoShell's `AttachScripting`,
`ApplySlotParameters` and `QueueSlotCompilation`.

```cpp
// Attaches every scripting component of `asset` to the nodes created for
// it. `nodes[i]` is the node created for scene node index i.
auto Attach(const data::SceneAsset& asset,
  std::span<const scene::SceneNode> nodes) -> SceneScriptAttachmentResult;
```

For each `ScriptingComponentRecord`:

1. Attach `ScriptingComponent` if absent.
2. For each hydrated slot, in record order: load the script asset, add the
   slot with its execution order, validate and apply overrides against the
   asset's declarations (diagnostic per rejected override), and resolve
   `kNode` indexes against `nodes`.
3. Resolve the executable through `ScriptSourceResolver`: embedded bytecode
   makes the slot ready at once; source is compiled through
   `IScriptCompilationService`, which marks the slot ready or failed with
   diagnostics.

The result lists per-slot outcomes and diagnostics. Missing assets, failed
resolution and compile errors leave the slot in `CompilationFailed` with its
diagnostic; the rest of the scene runs. The attachment registers the scene's
script assets with source reload (§6).

## 6. Source reload

Enabled by `ngin.scripting.hot_reload` with the script source roots.

The cooker accepts `compile` together with `external` storage: the asset
embeds bytecode and records the source path. Resolution prefers the source
when a source root contains it (development), and otherwise runs the
bytecode (shipped content). Embedded-source storage remains for content that
must not depend on files.

1. The engine's `ScriptHotReloadService` detects a changed `.lua`/`.luau`
   file and hands it to the scripting module
   (`IAsyncEngine::SetScriptSourceChangedHandler`), which maps it to the
   scene's script assets whose recorded source path resolves to it.
2. Scripting re-reads and recompiles the source through the analyzer and
   compiler (declarations and diagnostics included).
3. On success, every slot using the asset receives the new executable through
   `ScriptingComponent::MarkSlotReady`; `OnScriptSlotChanged` ends the old
   instance and starts a new one (§3.3). Declarations from the new source
   replace the asset's for default resolution.
4. On failure, running slots keep their last good executable, and the
   diagnostics are published. The next successful save recovers.

The content-side bytecode-only notification and `UpdateBytecode` are removed.

## 7. Analysis API for tools

`scripting::ScriptAnalyzer` parses source with the Luau AST and never runs
it.

```cpp
struct ScriptAnalysisRequest {
  std::string_view source_name; // virtual or project-relative path
  std::span<const std::byte> source;
  AnalysisLevel level; // kDeclarations, kLint, kTypes
};
auto Analyze(const ScriptAnalysisRequest&) -> ScriptAnalysis;
// ScriptAnalysis: declarations (with hints), diagnostics, has_errors
```

The cooker, the compile service and the editor (through Interop) all use it,
so every surface reports the same declarations and the same diagnostics.
It lives in `Oxygen.Scripting.Analysis`, which links Base, Data and the Luau
AST but not the scripting runtime, so the cooker and Interop use it without
the engine or the Luau VM. A chunk without `oxygen.script`, such as a module
other scripts load, declares no parameters and is not an error.
Level `kTypes` needs the API definitions described in
[script diagnostics](script-diagnostics.md#5-type-checking).

## 8. Migration

- DemoShell and RenderScene use scene instantiation and
  `SceneScriptAttachment`; their scripting code is deleted.
- Example scripts move to `oxygen.script`, `self` and hooks; their sidecars
  use the new parameter types where a GUID string or node name stood.
- `oxygen.scene` gains `active_camera()` and `set_active_camera(node)`, and
  `oxygen.input` gains `activate_context` and `deactivate_context`
  ([scene start state](../runtime-core/scene-instantiation-and-player.md#scene-start-state)).
- The input system identifies actions and contexts by asset key; names are
  labels. `oxygen.input.on_action` takes an action asset reference only, the
  name form is removed, and two actions that share a name stay distinct.
  Example scripts declare their actions as `asset` parameters.
- `scene-scripting-api-v1.md` §3.1 and §8 and
  `scene-scripting-cooking-design.md` §4.2 defer to this document for
  parameters, hooks and attachment.

## 9. Exit gates

| ID    | Check                                                                                                                                                       |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SSH-1 | Analyzer: every declaration rule passes and fails with the expected diagnostic and span.                                                                    |
| SSH-2 | Cook: declarations round-trip; a sidecar override that violates a declaration fails with its JSON path; asset parameters join the dependency closure.       |
| SSH-3 | Hooks: `on_start` precedes the first `on_gameplay`; `on_destroy` runs on slot removal, node destruction, scene unload and reload; connections are released. |
| SSH-4 | Order: instances run by execution order, node order, slot position.                                                                                         |
| SSH-5 | Attachment: a scene with valid, missing, failing and override-invalid slots attaches, runs the valid ones and reports the rest.                             |
| SSH-6 | Reload: a source edit swaps running slots; a broken edit keeps the last good executable and publishes diagnostics.                                          |
| SSH-7 | RenderScene runs every maintained scripted example scene with the new scripts and no DemoShell scripting code.                                              |
