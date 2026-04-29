# Property Pipeline Redesign — Critique & Proposal

> Status: Design proposal, ready for implementation planning.
> Scope: Authored properties flowing from the WinUI Inspector through the
> editor command layer, persistence, and the embedded engine. Defines
> what must change and what must not.
> Audience: Editor team, engine team, and anyone reviewing the proposal
> before it becomes an implementation milestone.

---

## 1. Executive summary

**The problem.** The current pipeline is layered correctly
(UI → VM → Command → Sync → Engine → PAK), but every layer reinvents
the *property* concept instead of receiving it as data. One conceptual
field exists in five places (engine struct, PAK record, C# component,
edit record, ViewModel observable) plus two derivative ones (validator,
undo closure). Nothing connects these projections at the type level, so
they drift silently. Multi-selection works but is copy-pasted per
ViewModel. Live sync is a wide re-attach masquerading as an atomic
edit. Engine-not-running fails silently to "skipped." Adding one float
to a component is 12–15 files and ~100 lines.

**The fix.** Promote *property* to a first-class, identified, typed
thing. Every property is declared once, in one schema, and that schema
drives everything else: the UI binding, the validator, the engine
command, the cooked PAK projection, the undo entry. The schema source
is the engine's existing JSON Schema files; the editor adds an overlay
file (separate, co-located, sibling-named) that carries UI-only
metadata in a reserved `x-editor-*` keyword namespace. The cooker is
unaffected. The engine does not rebuild when a label changes.

**What is preserved, exactly as today.**

- TimeMachine (the undo/redo framework). It is fit for purpose. The
  proposal uses it the way its README documents (§5.5). The symmetric
  identity `redo(OP) == undo(UOP) == OP` holds *by construction*
  because apply and restore are the **same** function applied to
  opposite snapshots.
- The EditorModule frame-staged command execution. `OnFrameStart` /
  `OnSceneMutation` / `OnPreRender` phases stay. The new
  `SetProperties` transport (§5.3) is a new `IEditorCommand` that
  drains at `OnSceneMutation`, the only phase where scene-graph
  component mutation is legal.
- The JSON / JSON5 authoring serialization and the PAK binary format.
  The proposal consolidates *where the schema is declared*, not the
  on-disk encodings.
- C++ component layouts. The proposal adds a property dispatch table
  at the interop boundary; it does not redesign components.
- Multi-selection semantics. Indeterminate / mixed values are
  preserved at every migration step (§7).

---

## 2. Verified facts driving the design

These are the constraints we validated against the codebase before
writing this proposal. Every later decision points back to these.

| # | Fact | Evidence |
| --- | --- | --- |
| F1 | The engine validates JSON authoring with `nlohmann_json` 3.11.3 + `pboettch/json-schema-validator` 2.4.0 (draft-07). | [`conanfile.py`](../../../projects/Oxygen.Engine/conanfile.py) lines 111-112; [`Cooker/CMakeLists.txt`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/CMakeLists.txt) |
| F2 | All cooker validation goes through `nlohmann::json_schema::json_validator` with `set_root_schema(...)`. | `ImportManifest.cpp`, `PakCatalogIo.cpp`, and per-descriptor builders in `Cooker/Import/Internal/` |
| F3 | Engine schemas are **embedded into the binary at build time** via `oxygen_embed_json_schemas(...)`. Adding or modifying a file listed in this CMake call **does** trigger an engine rebuild. | [`Cooker/CMakeLists.txt`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/CMakeLists.txt); [`PakTool/CMakeLists.txt`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Tools/PakTool/CMakeLists.txt); [`GenerateEmbeddedJsonSchemas.cmake`](../../../projects/Oxygen.Engine/cmake/GenerateEmbeddedJsonSchemas.cmake) |
| F4 | Engine schemas declare `"$schema": "http://json-schema.org/draft-07/schema#"`. Draft-07 mandates that validators ignore unknown keywords (treat as annotations). | [`oxygen.material-descriptor.schema.json`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json) and siblings |
| F5 | The C# editor side has **no JSON-Schema validator dependency today**. Centrally-managed packages list no `JsonSchema.Net` / `NJsonSchema` / `Newtonsoft.Json.Schema` / `Manatee.Json`. | [`Directory.packages.props`](../../../Directory.packages.props) |
| F6 | `EditorModule` runs scene-graph mutations in `OnSceneMutation` only; `OnFrameStart` is reserved for view/root sync; `OnPreRender` is read-only with respect to the scene. Commands are produced via `ICommandFactory` / `CommandFactory`. | [`EditorModule.h`](../../../projects/Oxygen.Editor.Interop/src/EditorModule/EditorModule.h); [`EditorModule.cpp`](../../../projects/Oxygen.Editor.Interop/src/EditorModule/EditorModule.cpp) |
| F7 | The current `SceneEngineSync` calls **wide methods** (e.g. `AttachDirectionalLight(intensity, angularSize, color, affectsWorld, castsShadows, exposureComp, envContribution, isSunLight)`) on every property edit. There is no per-property command vocabulary at the wire. | [`SceneEngineSync.cs`](../../../projects/Oxygen.Editor.WorldEditor/src/Services/SceneEngineSync.cs) |
| F8 | `TimeMachine` is the established undo/redo framework, with documented patterns: Pattern 2 (`AddChange(Action<TArg>, arg)`) and Pattern 3 (snapshot + apply/restore). | [`TimeMachine/README.md`](../../../projects/TimeMachine/README.md) |

---

## 3. Defects in the current implementation

### 3.1 Five parallel representations of one field

For a single conceptual property (e.g. `DirectionalLight.IntensityLux`):

| Layer | Representation | File |
| --- | --- | --- |
| Engine runtime | C++ component field, mutated via wide `AttachDirectionalLight(...)` | [`PakFormat_world.h`](../../../projects/Oxygen.Engine/src/Oxygen/Data/PakFormat_world.h) + interop |
| Cooked PAK | `DirectionalLightRecord.intensity_lux` | [`PakFormat_world.h`](../../../projects/Oxygen.Engine/src/Oxygen/Data/PakFormat_world.h) |
| Editor model | `DirectionalLightComponent.IntensityLux` | [`LightComponents.cs`](../../../projects/Oxygen.Editor.World/src/Components/LightComponents.cs) |
| Edit record | `Optional<float> IntensityLux` | [`ComponentEditRecords.cs`](../../../projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/ComponentEditRecords.cs) |
| ViewModel | `[ObservableProperty] float IntensityLux` + `IntensityLuxIsIndeterminate` | [`DirectionalLightViewModel.cs`](../../../projects/Oxygen.Editor.WorldEditor/src/Inspector/DirectionalLightViewModel.cs) |

Plus a per-edit validator and a per-edit undo closure: seven assertions
of "this field exists." Adding a property requires lockstep edits across
all of them; any drift is silent until runtime.

### 3.2 Live sync is a re-attach, not an edit (F7)

Every property change re-pushes the entire component via a wide method.
There is no per-property command shape. Any incorrect C# read (unit
conversion, Quaternion↔Euler, default value) gets re-pushed across
unrelated edits. Engine state coupled to a field churns unnecessarily.

### 3.3 `NotImplementedException` as control flow

`SceneEngineSync` catches `NotImplementedException` and reports
`Unsupported`. This conflates "no adapter for this component yet" with
"real bug threw `NotImplementedException`." Users discover unsupported
components only by editing them.

### 3.4 Sequential per-node `await`

`SyncEditedNodesAsync` awaits each node's sync sequentially. With N
selected nodes this is N round-trips on the UI thread continuation,
with no transactional rollback if node K fails after K-1 have already
been pushed.

### 3.5 Re-entrancy guarded by a bool

`isApplyingEditorChanges` short-circuits `OnXChanged` while the VM is
refreshed. Standard MVVM hack; fragile against async completions,
multi-property setters firing partial notifications, and dispatcher
hops.

### 3.6 Mixed-value state is two coupled properties

`PositionX = 0f` plus `PositionXIsIndeterminate = true` carries the
indeterminate state. Any code path that fires `OnPositionXChanged`
between the two writes interprets the placeholder `0f` as a real edit.

### 3.7 Engine-not-running silently diverges

If the engine isn't running, sync returns `SkippedNotRunning`; the
model is mutated and dirty, but the viewport shows old values. There
is no banner, no replay queue, no observable divergence.

### 3.8 Multi-select `MixedValues` reimplemented per VM

Every component VM hand-codes the same pattern. Mechanical, should be
generic.

### 3.9 Selection captured at session begin, applied at commit

`EditSessionToken.Begin()` snapshots node ids; `Commit()` applies the
buffered edit to those ids. If selection mutates during a drag, the
commit silently goes to the *original* set.

### 3.10 Validation lives in three places

XAML control constraints (`Minimum="0"`), VM partial methods, and
`ValidateXEdit` in the command service. Three potentially disagreeing
sources of truth.

---

## 4. Architectural root cause

The system has no first-class concept of "a property." Each layer
holds its own *projection* of properties — observable, optional,
packed, lambda, JSON — and stitches them together per field.
Consequences cascade:

- No `PropertyId<T>` to thread through binding, validation, sync,
  undo, and cooking → each layer rediscovers identity by string or
  member access.
- No declarative schema → the compiler cannot tell us when projections
  disagree.
- No partial-update transport (F7) → every IPC call is a wide write.
- No transactional grouping at the engine boundary → multi-node /
  multi-property edits cannot be applied or rolled back atomically.
- Cooking and authoring evolve independently → schema drift is
  detected at load time, or never.

Promote *property* to a first-class, identified, typed thing and the
rest of the pipeline reduces to plumbing.

---

## 5. Proposed design

### 5.1 First-class property descriptors

A per-component schema names every authored field with a typed
identity:

```text
PropertyId<float>      Transform.PositionX
PropertyId<Quaternion> Transform.Rotation        // presented as Euler
PropertyId<float>      DirectionalLight.IntensityLux
PropertyId<Color>      DirectionalLight.Color
PropertyId<AssetRef>   Geometry.GeometryAsset
```

Each `PropertyDescriptor<T>` binds five roles in one declaration:

| Role | Conceptual signature |
| --- | --- |
| Reader | `(component) -> T` |
| Writer | `(component, T) -> void` |
| Validator | `(T) -> ValidationResult` (pure) |
| Sync | `(nodeId, T) -> EngineCommand` |
| PAK | offset + encoding for the cooked record |

Anything that needs a property goes through this descriptor. There
are no field-name strings at use sites; identity is `PropertyId<T>`
and the type system enforces it.

### 5.2 Generic edit record

Today's `TransformEdit { Optional<float> PositionX, Optional<float> PositionY, ... }`
collapses to one shape:

```text
PropertyEdit = Map<PropertyId, Value>
```

Absence is encoded by absence in the map; the `Optional<>` wrapper
disappears. Multi-property edits (e.g. dragging X *and* Z together)
are multi-entry edits. Multi-node edits are the same `PropertyEdit`
applied to a different node set.

### 5.3 Uniform partial-update transport, executed at the right frame stage

Replace wide interop methods with one generic command shape:

```text
SetProperties(nodeId, [(PropertyId, Value), ...])
```

Properties:

- One command shape, dispatched in the engine via a property-id table
  to the right component setter.
- Real per-property atomicity at the wire.
- Trivially batches many properties of one node, or one property
  across many nodes, into one IPC round-trip.
- The engine becomes responsible for idempotent partial updates, not
  for re-attaching components on every edit.

**This plugs into the existing EditorModule frame-stage execution
model (F6); it does not bypass it.** Concretely:

- `SetProperties` is implemented as a new `IEditorCommand` produced
  by `ICommandFactory` alongside today's commands.
- The C# interop layer enqueues these commands through the same
  thread-safe path that today's `OxygenWorld` interop uses.
- The `EditorModule` drains and executes them at `OnSceneMutation` —
  the only phase where scene-graph component mutation is legal. It
  does **not** execute them at `OnFrameStart` (view/root sync) or
  `OnPreRender` (read-only).
- Preview-only sync during a drag uses the same command type with a
  flag (or a sibling `PreviewProperties` command) so frame-stage
  semantics are identical; only the C#-side history-recording
  behavior differs.

What changes is the *vocabulary* of commands (one generic command
instead of many wide ones), not the *execution model*.

### 5.4 Multi-selection becomes a binding type

Introduce `PropertyBinding<T>`:

- Holds: current value, `IsMixed`, the set of nodes it edits, the
  `PropertyId<T>`.
- The inspector view binds against `PropertyBinding<T>`, not against
  raw `[ObservableProperty]` pairs.
- "Indeterminate" is a state of the binding, not a parallel bool.
- Setting the binding produces one `PropertyEdit` entry against all
  selected nodes — multi-select is preserved by construction; it is
  not possible to re-implement it incorrectly.

The existing `MixedValues` helper folds into this type. Per-VM
mixed-value plumbing disappears.

### 5.5 Undo via TimeMachine — semantics first, then code

TimeMachine (F8) is preserved exactly. The undo/redo identity for a
property edit is symmetric:

```text
undo(OP)         = UOP
undo(UOP)        = OP
redo(OP)         = undo(UOP) = OP
```

For a property edit, `OP` is "set property P from `V_before` to
`V_after` on node set N", and `UOP` is the **same** operation with
`V_before` and `V_after` swapped. They are not two different code
paths — they are one code path applied to two snapshots. That is
what makes the identity hold structurally rather than by code review.

The canonical primitive (TimeMachine Pattern 3 using Pattern 2):

```csharp
record PropertyOp(
    IReadOnlyList<SceneNode> Nodes,
    PropertyEdit Before,
    PropertyEdit After);

void ApplyPropertyOp(PropertyOp op)
{
    PropertyApply.Apply(op.Nodes, op.After);   // model + queue engine cmd
    historyKeeper.AddChange("undo", RestorePropertyOp, op);
}

void RestorePropertyOp(PropertyOp op)
{
    PropertyApply.Apply(op.Nodes, op.Before);  // same code, swapped sides
    historyKeeper.AddChange("redo", ApplyPropertyOp, op);
}
```

Mapped to the identity:

- `OP` ≡ `ApplyPropertyOp(op)` with `op = (Nodes, Before=V1, After=V5)`.
  After it runs, the property is `5`; the undo stack carries
  `RestorePropertyOp(op)`.
- `undo(OP)` ≡ `RestorePropertyOp(op)` runs `Apply(Nodes, V1)`.
  Property is `1`; the redo stack carries `ApplyPropertyOp(op)`.
- `redo(OP)` ≡ `undo(UOP)` ≡ `ApplyPropertyOp(op)` runs
  `Apply(Nodes, V5)`. Property is `5` — *bit-identical* to the
  result of the original `OP`, because the same `op` instance and
  the same `PropertyApply.Apply` function are used. There is no
  second "redo" implementation that could drift.

Properties of this approach:

- Inverse is not a hand-written mirror; it is the *same* function
  (`PropertyApply.Apply(nodes, edit)`) applied to the snapshot from
  the opposite side.
- One user gesture → one `AddChange` entry (the rule TimeMachine's
  README states).
- The operand `PropertyOp` carries everything needed to replay the
  inverse — no reliance on "current selection" at undo time.
- Composes naturally with TimeMachine transactions and change sets
  for gestures spanning multiple components or nodes.

A generic property test enforces the identity for every registered
descriptor:

```text
for each PropertyDescriptor P, for each sample (v1, v2):
    s0 = state
    apply(P, v1 -> v2)        // OP
    apply(P, v2 -> v1)        // UOP (== undo)
    apply(P, v1 -> v2)        // OP again (== redo)
    assert state == s0_after_first_apply
```

### 5.6 Sessions become a generic commit-group controller

Today: `EditSessionToken` + per-VM `activeSessions` dictionary +
`editGate` semaphore + `wheelIdleCommits` timers — roughly 300 lines
of state machine duplicated per ViewModel.

Replacement: one `CommitGroupController` that:

- Opens on drag / begin-edit / wheel-tick with a typed `PropertyId`
  and node set.
- Captures `Before` once at open.
- Buffers preview values; throttled preview sync (~16 ms) issues
  fire-and-forget `SetProperties` to keep the viewport live, **without
  pushing history**.
- On commit: computes `After`, builds `PropertyOp(Before, After)`,
  calls `ApplyPropertyOp`, registers the inverse.
- On cancel: restores `Before` directly, registers nothing.

Mouse-wheel idle-commit (250 ms) is a property of the controller, not
a copy-paste in every numeric VM.

### 5.7 Engine state is observable, never silent

The sync transport carries an explicit policy: `RequireRunning`,
`PreviewOnly`, `Buffered`. When the engine isn't running:

- Edits are buffered with their `PropertyEdit`s.
- The inspector shows a banner: "Engine not running — N pending edits."
- On attach, buffered edits replay in order.
- The user is never in a state where editor and engine disagree
  without being told.

### 5.8 Selection is locked during a session

Opening a `CommitGroup` snapshots selection AND prevents selection
mutation until commit/cancel. If the UI must allow selection change
during a drag, the controller aborts the session with a visible
toast — never silently edits yesterday's selection.

### 5.9 Validation is single-sourced from the descriptor

The descriptor's `Validator` is the only authority. The XAML control's
constraint becomes a *hint* (e.g. spinner range) derived from the
descriptor, not an independent rule. The command service no longer
hand-codes `ValidateXEdit` blocks — it asks the schema.

### 5.10 PAK round-trip is enforced from the same descriptor

Each descriptor declares its PAK projection (offset + encoding). A CI
test enumerates every descriptor and round-trips a representative
sample through `cook → PAK → load`, asserting equality. PAK drift
becomes impossible to merge.

The engine schema files described in §5.11 are the source of truth
for *what fields exist and what they mean*; the descriptor's PAK
projection is the C#-side binding to the existing C++ record layout.

---

## 5.11 One descriptor schema for editor and engine

### 5.11.1 Decision

> The engine's existing JSON Schema files are the **single source of
> truth** for what fields exist, their types, and their constraints.
> The editor adds a sibling **overlay file** (`*.editor.schema.json`)
> that uses standard JSON Schema composition (`$ref`, `allOf`) plus
> annotation keywords in the reserved `x-editor-*` namespace to
> attach UI-only metadata. The cooker is unaffected. The engine does
> not rebuild when overlay files change.

This is grounded directly in F1–F5.

### 5.11.2 Why this is safe (verified, not hypothetical)

- **The cooker ignores `x-editor-*` keywords by spec.** `nlohmann_json`
  with `pboettch/json-schema-validator` 2.4.0 (F1) implements draft-07
  semantics. Draft-07 mandates that validators ignore unknown
  keywords (treat as annotations). The cooker, however, is **never
  pointed at overlay files** — see below — so this mandate is a
  defense-in-depth property, not the primary safety mechanism.
- **The engine does not rebuild on overlay changes if and only if
  overlay files are not embedded.** Engine schemas are embedded into
  the binary at build time via `oxygen_embed_json_schemas(...)`
  (F3). Adding or modifying a file listed in those CMake calls
  *does* trigger an engine rebuild. The contract that prevents
  rebuilds on overlay edits is purely mechanical:
  **`*.editor.schema.json` MUST NOT appear in any
  `oxygen_embed_json_schemas(...)` invocation.** A CI lint enforces
  this (§5.11.7 #4).
- **Editor-side validation requires a new dependency.** F5 confirms
  no JSON Schema validator package exists today. The proposal adds
  one: see §5.11.5.

### 5.11.3 Mechanism

Standard JSON Schema draft-07 keywords (no dialect changes):

- `$ref` — overlay `$ref`s the engine schema as the contract.
- `allOf` — composes the overlay's annotation tree with the engine's
  constraint tree without redefining anything.
- `$id`, `$schema`, `title`, `description`, `definitions`,
  `properties` — for structure only; never to redeclare types or
  constraints.
- **`x-editor-*` annotation keywords** — UI-only, in a reserved
  namespace.

The overlay file:

1. `$ref`s the engine schema as the primary contract.
2. Declares a parallel `properties` tree, mirroring the engine
   schema's structure, that carries `x-editor-*` annotations only.
3. May tighten a constraint via `allOf` only when the editor
   genuinely needs to forbid something the cooker tolerates (rare;
   require explicit reviewer approval).

### 5.11.4 File layout and naming

| Concern | Location | Owner |
| --- | --- | --- |
| Engine schemas | `projects/Oxygen.Engine/src/Oxygen/Cooker/Import/Schemas/*.schema.json` | Engine team |
| Editor overlays | `projects/Oxygen.Engine/src/Oxygen/Cooker/Import/Schemas/*.editor.schema.json` (sibling files) | Editor team |
| C# loader / merger / descriptor factory | `projects/Oxygen.Editor.Schemas/` (new project) | Editor team |

Co-locating overlay files with engine schemas:

- Forces them to version together (same git revision, same PR
  review whenever an engine schema changes).
- Makes the `$ref` to the engine schema a relative path with no
  resolver gymnastics.
- Lets the §5.11.7 coverage CI gate iterate one folder.

The **only** file-system rule that prevents engine rebuilds on
overlay changes is: `*.editor.schema.json` must not be listed in any
`oxygen_embed_json_schemas(...)` call in
[`Cooker/CMakeLists.txt`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/CMakeLists.txt)
or [`PakTool/CMakeLists.txt`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Tools/PakTool/CMakeLists.txt).

### 5.11.5 New editor project: `Oxygen.Editor.Schemas`

A new shared library, peer to `Oxygen.Editor.Projects` and
`Oxygen.Editor.World`:

```text
projects/Oxygen.Editor.Schemas/
  src/
    Oxygen.Editor.Schemas.csproj
    EditorSchemaCatalog.cs        # discovers + loads all engine schemas
                                  # and overlays from the Schemas folder
    EditorSchemaOverlay.cs        # merges engine + overlay by JSON Pointer
    PropertyDescriptor.cs         # the typed descriptor (§5.1)
    PropertyDescriptor`1.cs
    EditorAnnotation.cs           # parsed x-editor-* surface
    PropertyId.cs                 # typed identity
    PropertyEdit.cs               # the generic edit map (§5.2)
    PropertyApply.cs              # the single apply function (§5.5)
    Bindings/
      PropertyBinding`1.cs        # multi-select binding (§5.4)
      MixedValue`1.cs             # collapses today's MixedValues helper
  tests/
    Oxygen.Editor.Schemas.Tests.csproj
    OverlayMergeTests.cs
    PropertyApplyIdentityTests.cs   # the §5.5 generic identity test
    OverlayCoverageTests.cs         # CI gate (§5.11.7 #1)
    AnnotationNamespaceLintTests.cs # CI gate (§5.11.7 #2)
    ValidatorParityTests.cs         # CI gate (§5.11.7 #3)
```

Why a new project (and not folding into an existing one):

- The same machinery is needed by every editor module that has a
  property inspector — World Editor, Material Editor, and any future
  module (animation, particle, terrain). Folding it into
  `Oxygen.Editor.WorldEditor` would force every other editor to
  depend on WorldEditor.
- `Oxygen.Editor.Projects` owns project lifecycle (creation, recents,
  validation), not the property pipeline.
- `Oxygen.Editor.World` is the scene domain model. The schema engine
  is generic over component type and is also used by non-scene
  editors.
- Keeping the schemas project pure logic (no WinUI dependency) lets
  unit tests run without a UI host.

Required new dependency (per F5):

```xml
<PackageVersion Include="JsonSchema.Net" Version="<latest 7.x>" />
```

Recommended choice: `JsonSchema.Net` (json-everything). Modern, .NET 9
compatible, draft-07 support, ignores unknown keywords by spec.
Single line in [`Directory.packages.props`](../../../Directory.packages.props).
Consumed only by `Oxygen.Editor.Schemas`.

How the schema files reach the assembly at runtime: a `<Content
Include="..\..\Oxygen.Engine\src\Oxygen\Cooker\Import\Schemas\*.schema.json"
Link="Schemas\%(FileName)%(Extension)" CopyToOutputDirectory="PreserveNewest" />`
glob in `Oxygen.Editor.Schemas.csproj`. Files remain owned by the
engine repo on disk; editing one in place serves both consumers.

### 5.11.6 Worked example: material descriptor

Engine schema:
[`oxygen.material-descriptor.schema.json`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json)
declares `parameters.metalness: number [0,1]`,
`parameters.base_color: vec4_01`, `domain: enum`, etc.
**Nothing in this file changes.**

Editor overlay (new file, sibling, *not* embedded):

```jsonc
// oxygen.material-descriptor.editor.schema.json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "oxygen://schemas/material-descriptor.editor.json",
  "title": "Oxygen Material Descriptor — Editor Overlay",

  // The instance must satisfy the engine schema first. The overlay
  // never redefines an engine constraint.
  "allOf": [
    { "$ref": "oxygen.material-descriptor.schema.json" }
  ],

  // Document-level UI metadata.
  "x-editor-icon": "material",
  "x-editor-doc-url": "https://docs.oxygen.dev/authoring/materials",

  // Parallel structure mirroring the engine schema, carrying only
  // x-editor-* keywords. The cooker never sees this file.
  "properties": {
    "domain": {
      "x-editor-label": "Domain",
      "x-editor-group": "Surface",
      "x-editor-order": 10,
      "x-editor-renderer": "enum-dropdown",
      "x-editor-enum-labels": {
        "opaque": "Opaque",
        "alpha_blended": "Alpha Blended",
        "masked": "Alpha Masked",
        "decal": "Decal",
        "ui": "UI",
        "post_process": "Post Process"
      },
      "x-editor-tooltip":
        "Selects the rendering domain. Changing this may invalidate cached shader permutations."
    },
    "alpha_mode": {
      "x-editor-label": "Alpha Mode",
      "x-editor-group": "Surface",
      "x-editor-order": 20,
      "x-editor-renderer": "enum-dropdown",
      // Editor-only conditional visibility. JSON Pointer + value list
      // keeps the parser trivial.
      "x-editor-visible-when": {
        "path": "/domain",
        "in": ["alpha_blended", "masked", "decal"]
      }
    },
    "parameters": {
      "x-editor-group": "Parameters",
      "properties": {
        "base_color": {
          "x-editor-label": "Base Color",
          "x-editor-group": "Parameters/Albedo",
          "x-editor-order": 10,
          "x-editor-renderer": "color-rgba",
          "x-editor-color-space": "srgb",
          "x-editor-tooltip":
            "Multiplied with the base color texture (if any)."
        },
        "metalness": {
          "x-editor-label": "Metallic",
          "x-editor-group": "Parameters/Surface",
          "x-editor-order": 20,
          "x-editor-renderer": "slider",
          "x-editor-step": 0.01,
          "x-editor-tooltip": "0 = dielectric, 1 = metal."
        },
        "roughness": {
          "x-editor-label": "Roughness",
          "x-editor-group": "Parameters/Surface",
          "x-editor-order": 30,
          "x-editor-renderer": "slider",
          "x-editor-step": 0.01
        },
        "ior": {
          "x-editor-label": "Index of Refraction",
          "x-editor-group": "Parameters/Advanced",
          "x-editor-order": 60,
          "x-editor-renderer": "numberbox",
          "x-editor-step": 0.001,
          "x-editor-advanced": true,
          // Editor-only soft cap. The engine schema only requires
          // ior >= 1.0; the spinner is capped at 3.0 for ergonomics.
          // Hand-edited higher values are still valid per the engine.
          "x-editor-soft-max": 3.0
        },
        "clearcoat_factor": {
          "x-editor-label": "Clearcoat Strength",
          "x-editor-group": "Parameters/Clearcoat",
          "x-editor-order": 70,
          "x-editor-renderer": "slider",
          "x-editor-advanced": true
        },
        "clearcoat_roughness": {
          "x-editor-label": "Clearcoat Roughness",
          "x-editor-group": "Parameters/Clearcoat",
          "x-editor-order": 71,
          "x-editor-renderer": "slider",
          "x-editor-advanced": true,
          "x-editor-visible-when": {
            "path": "/parameters/clearcoat_factor",
            "gt": 0.0
          }
        },
        "transmission_factor": {
          "x-editor-label": "Transmission",
          "x-editor-group": "Parameters/Transmission",
          "x-editor-order": 80,
          "x-editor-renderer": "slider"
        },
        "thickness_factor": {
          "x-editor-label": "Thickness",
          "x-editor-group": "Parameters/Transmission",
          "x-editor-order": 81,
          "x-editor-renderer": "slider",
          "x-editor-visible-when": {
            "path": "/parameters/transmission_factor",
            "gt": 0.0
          }
        },
        "double_sided": {
          "x-editor-label": "Double Sided",
          "x-editor-group": "Parameters/Surface",
          "x-editor-order": 100,
          "x-editor-renderer": "toggle"
        },
        "unlit": {
          "x-editor-label": "Unlit",
          "x-editor-group": "Parameters/Surface",
          "x-editor-order": 110,
          "x-editor-renderer": "toggle",
          "x-editor-warn-when-true":
            "Unlit materials ignore lighting and shadows."
        }
      }
    },
    "textures": {
      "x-editor-group": "Textures",
      "properties": {
        "base_color": {
          "x-editor-label": "Base Color Map",
          "x-editor-renderer": "asset-picker",
          "x-editor-asset-kind": "texture",
          "x-editor-asset-color-space": "srgb"
        },
        "normal": {
          "x-editor-label": "Normal Map",
          "x-editor-renderer": "asset-picker",
          "x-editor-asset-kind": "texture",
          "x-editor-asset-color-space": "linear",
          "x-editor-asset-channel-hint": "tangent-space-normal"
        },
        "metallic": {
          "x-editor-label": "Metallic Map",
          "x-editor-renderer": "asset-picker",
          "x-editor-asset-kind": "texture"
        },
        "roughness": {
          "x-editor-label": "Roughness Map",
          "x-editor-renderer": "asset-picker",
          "x-editor-asset-kind": "texture"
        }
      }
    },
    "shaders": {
      "x-editor-label": "Custom Shader Stages",
      "x-editor-group": "Advanced",
      "x-editor-advanced": true,
      "x-editor-renderer": "list-editor"
    }
  }
}
```

What this overlay does and does not do:

- **Does not redeclare types or constraints.** Every type, range,
  enum membership, and required-ness is enforced by
  `allOf: [{ "$ref": engine_schema }]`. If the engine tightens
  `metalness` or renames a field, the cooker keeps validating
  correctly without overlay edits; the overlay is touched only when
  the *UI label* needs to change.
- **Adds annotations only.** Every keyword is in the `x-editor-*`
  namespace.
- **Conditional visibility is editor-only.** The cooker happily
  accepts `clearcoat_roughness` regardless of `clearcoat_factor`;
  the editor merely hides the row when it has no visual effect.
- **Soft caps are editor-only.** `x-editor-soft-max` clamps the
  spinner range without forbidding hand-edited higher values. The
  engine schema's `minimum: 1.0` remains the only validation
  authority.

### 5.11.7 Workflow and CI gates

**Engine team workflow:**

1. Edit `*.schema.json` files only.
2. Treat any addition / rename / type / constraint change as a
   contract change. Update cooker, importer, runtime, and PAK records
   together (existing process).
3. Never edit `*.editor.schema.json`. Engine CI does not parse them.

**Editor team workflow:**

1. UI-only iteration (label, group, order, renderer, tooltip,
   `x-editor-*` annotation, conditional visibility, soft cap) is an
   *editor-only* PR touching `*.editor.schema.json`. **No engine
   rebuild, no cooker re-run, no PAK regeneration.**
2. Adding a new field is a two-PR change: (a) engine PR adding the
   field to `*.schema.json` and the importer/runtime; (b) editor PR
   adding the overlay entry and the JSON-Pointer entry in the
   binding table that maps the descriptor to its engine command id
   and PAK projection.

**CI gates** (all cheap, all mechanical):

1. **Overlay coverage.** For every leaf path in the engine schema,
   the overlay must declare at least `x-editor-label` and
   `x-editor-renderer`, OR the leaf must appear in an
   `x-editor-hidden-paths` allowlist in the overlay (e.g.
   `shader_hash`, content-addressed, not authored). Editor-side
   build error.
2. **Annotation namespace lint.** The overlay file may contain only
   `x-editor-*` keywords plus the standard composition keywords
   (`$schema`, `$id`, `$ref`, `allOf`, `properties`, `definitions`,
   `title`, `description`, `$comment`). Anything else fails the
   editor build, so an overlay change cannot accidentally redefine
   engine semantics.
3. **Validator parity.** Every authored sample asset under the
   editor's `tests/data/` validates against (a) the engine schema
   alone and (b) `engine_schema + overlay`. The two must agree on
   accept/reject for every sample. Drift means the overlay sneaked
   in a real constraint — CI failure.
4. **Engine-rebuild isolation.** A linter scans
   `Cooker/CMakeLists.txt` and `PakTool/CMakeLists.txt` and
   asserts no `*.editor.schema.json` appears in any
   `oxygen_embed_json_schemas(...)` invocation. This is the single
   file-system rule that guarantees engine artifacts do not depend
   on overlay files.

### 5.11.8 What this rules out and what it preserves

**Rules out:**

- Adding a property to a C# component class without a matching
  engine field. The schema is the gate.
- Diverging units / ranges between editor and cooker. The engine
  schema is the only validation authority; overlays cannot loosen
  it (lint #2 prevents redeclaration).
- Writing a separate `*ComponentData` DTO that duplicates the C#
  component just for serialization.
- Modifying engine artifacts to ship a label change.

**Preserves:**

- JSON / JSON5 authoring serialization encoding, hydrate / dehydrate
  semantics, and PAK binary format — unchanged.
- Cooker responsibilities (asset linking, validation, layout) —
  unchanged.
- Engine schemas' current shape and content — unchanged. The overlay
  is purely additive, in a separate file, in a reserved namespace.

---

## 6. Adding a new property after the redesign

`DirectionalLight.CustomIntensityScale` example:

1. Add the field to the engine schema and the C++ component
   (engine PR).
2. Add the overlay entry under
   `oxygen.directional-light.editor.schema.json`:
   `x-editor-label`, `x-editor-renderer`, `x-editor-group`,
   `x-editor-step` (~5 lines, editor PR).
3. Add the JSON-Pointer → `(engine-command-id, PAK offset)` row in
   the binding table (1 line, editor PR).
4. Add the property setter to the engine's property dispatch table
   used by `SetProperties` (~3 lines C++, in the engine PR).

XAML, multi-select, undo/redo, throttling, validation surfacing,
PAK round-trip, engine sync — all derive from the descriptor and
need no further changes. Custom UI is opt-in via a registered
renderer for an `x-editor-renderer` value.

Today: 12–15 files, 80–120 LOC.
After: 4 files, ~15 LOC.

---

## 7. Migration plan

Incremental, non-breaking, multi-select preserved at every step (the
only thing that moves is *where* mixed-value logic lives).

1. **Add `Oxygen.Editor.Schemas` project + `JsonSchema.Net`
   dependency.** Implement engine-schema + overlay loader and
   `PropertyDescriptor<T>` model. No behavior change.
2. **Author the first overlay**
   (`oxygen.material-descriptor.editor.schema.json`) and prove the
   merge + descriptor generation against the existing material
   schema in unit tests. Adds CI gates §5.11.7 #2 and #3.
3. **Add the CMake lint** (CI gate §5.11.7 #4) to guarantee
   engine-rebuild isolation.
4. **Implement `SetProperties` as a new `IEditorCommand`** plumbed
   through `ICommandFactory` and drained at `OnSceneMutation`
   (F6). Run it in parallel with existing wide methods.
5. **Migrate Transform** end-to-end onto the schema:
   `PropertyEdit`, `PropertyBinding<T>`, `CommitGroupController`,
   `PropertyOp` undo (§5.5). Validate parity against current
   indeterminate placeholder behavior on drag, wheel, keyboard.
6. **Add the engine-not-running buffering policy** (§5.7) with
   inspector banner.
7. **Add the schema↔PAK round-trip test** (§5.10) so cooking cannot
   silently diverge.
8. **Add overlay coverage CI gate** (§5.11.7 #1) once Transform's
   overlay is the reference.
9. **Migrate remaining components** (lights, cameras, geometry,
   environment) onto the schema. Each migration is local and
   independently shippable.

---

## 8. Non-goals

- Replacing TimeMachine. The undo/redo framework is preserved; this
  proposal uses it per its documented patterns (§5.5).
- Replacing MVVM Toolkit observability. `[ObservableProperty]` stays
  inside `PropertyBinding<T>`; the surface to the view does not
  change.
- Changing JSON / JSON5 authoring serialization encoding or the PAK
  binary format. The proposal consolidates *where the schema is
  declared* (§5.11), not how data is encoded on disk or on the wire.
- Changing the EditorModule frame-stage execution model.
  `OnFrameStart` / `OnSceneMutation` / `OnPreRender` and
  `ICommandFactory` are preserved; `SetProperties` is an additional
  command in the existing pipeline (§5.3, F6).
- Changing C++ component layouts. The proposal adds a property
  dispatch table at the interop boundary that routes
  `(PropertyId, Value)` pairs to existing setters; it does not
  redesign components.
- Modifying engine schema files to embed `x-editor-*` content.
  Overlay files are separate (§5.11.4) so engine builds are not
  invalidated by UI iteration.

---

## 9. What this buys us

- **Atomicity at the right granularity.** Per `(nodeId, propertyId)`
  at the wire (F7 → §5.3), batched into one IPC round-trip per
  commit, executed at the only legal frame stage (F6).
- **Reliability.** Undo cannot drift from apply because they share
  one function (§5.5). Sync cannot silently re-push wrong fields
  (§5.3). Engine-not-running is observable (§5.7).
- **Extensibility.** New property = one engine-schema entry + one
  overlay entry + one binding row. New component = one schema + one
  overlay. New editor widget = one renderer mapping for an
  `x-editor-renderer` value.
- **Testability.** The schema is data, so generic property tests
  cover *every* registered field automatically:
  `apply ∘ undo == id` (§5.5), validator-parity (§5.11.7 #3), PAK
  round-trip (§5.10).
- **Isolation.** Editor UI iteration does not rebuild the engine
  (§5.11.2, §5.11.7 #4).
- **Multi-select preserved and strengthened.** Indeterminate is a
  typed state of `PropertyBinding<T>`, not two coupled fields the
  developer can desync.

The current design's instinct (UI → VM → command → sync → engine) is
correct. The defect is that each layer reinvents the property concept
and pays the integration cost N times per field. Promote *property*
to a first-class, identified, typed thing — backed by the engine's
own JSON Schema, augmented by an editor overlay, executed through the
existing EditorModule command pipeline, and undone by the existing
TimeMachine framework — and the rest of the pipeline reduces to
plumbing around it.
