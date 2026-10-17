# Scene edits: test specification

Status: **draft**.

Scope: the unit and functional tests of `Oxygen.Editor.Scenes.Tests` for the
design in [scene-edits.md](scene-edits.md). No integration tests: no engine, no
interop, no WinUI host. The engine projector, TimeMachine and Documents test
their own changes in their own projects (scene-edits.md section 8).

Where the current service and the design differ, the design wins. Rules kept
from the current service are listed because users rely on them; each is tested
through its replacement edit.

## 1. Conventions

- **Unit**: one edit, helper or frame step, through the public API
  (`SceneAuthoring`, the edits, the `Nodes/` helpers). **Functional**: a user
  action across several pieces (document decode, authoring, history, gestures,
  batches, relocation).
- A section number in parentheses, such as (3.5), is the section of
  [scene-edits.md](scene-edits.md) that states the contract.
- A test name states its scenario and its outcome: `Scenario_Outcome`, or
  `Member_Scenario_Outcome` when the class covers several members. It never
  repeats the class's subject. IDs are stable review handles, not names.
- A data-driven test lists its rows by display name; each row is one scenario of
  the test, never a loop inside one test.
- **Unchanged**: `Dehydrate()` serializes to the same JSON as before, layout
  included; the undo and redo counts are the same; no `Changed` was raised.
- **Round trip**: execute → state A; undo → before; redo → A; undo → before,
  compared through `Dehydrate()`; each step raises the changes of what it
  applied.
- **Diagnostic node**: a diagnostic about one node names it in `Node`; one about
  a folder, the layout or the scene names none (3.8). Tests assert it either
  way.
- **Refused/Invalid/Retired** assert the outcome, the diagnostic `Code` in
  brackets (a `SceneEditCodes` constant), its `Node` and, for value rules, its
  `Property`; and Unchanged. Warnings assert their code the same way. Tests
  never assert a `Message` (3.8).

## 2. Project layout

One project, the existing `tests/Oxygen.Editor.Scenes.Tests.csproj` (MSTest SDK
4, default tier `Unit`, references only `src`, Moq for foreign ports). Unit and
functional tests share it; the folder says which kind a class holds. No files on
disk: scenes are built in code and serialized in memory.

- Folders mirror the `src` layout (scene-edits.md section 6); namespaces follow
  folders under `Oxygen.Editor.Scenes.Tests`. One exception: `Properties/` holds
  only the assembly attributes, so the tests of `src/Properties` (descriptors
  and component rules) go in `PropertyRules/`.
- One class per subject, `<Subject>Tests`; a class past about 40 tests splits
  into partials by scenario (`ReparentNodesTests.PreserveWorld.cs`).
- Public members only; test classes are public, so `DiscoverInternals` goes.
- `[Parallelize]` stays: every test builds its own scene, history and fixture.
- Each class lands with the design task that brings its subject; a task is done
  when its classes pass.

```text
tests/
  Oxygen.Editor.Scenes.Tests.csproj
  Properties/            AssemblyInfo.cs, GlobalSuppressions.cs only
  PropertyRules/         component rules, through SetProperties
  TestSupport/           namespace Oxygen.Editor.Scenes.Tests.TestSupport
  Authoring/             frame, undo, batches, change stream, gestures, threads
  Edits/                 EditContractTests, then one folder per edit family
    Properties/  Components/  Layout/  Hierarchy/  Assets/  Settings/  EditorView/
  Nodes/                 templates, copies, clipboard, SceneComponents
  Documents/             SceneContent, decode, reload, relocation
  Scenarios/             multi-step user actions
```

### 2.1 Test classes

| Folder              | Class                          | Kind       | Task  |
| ------------------- | ------------------------------ | ---------- | ----- |
| `Authoring/`        | `SceneAuthoringTests`          | unit       | 5     |
|                     | `UndoRedoTests`                | unit       | 5     |
|                     | `SceneEditBatchTests`          | unit       | 5     |
|                     | `ChangeStreamTests`            | unit       | 5     |
|                     | `SceneThreadTests`             | functional | 5     |
|                     | `SceneGestureTests`            | unit       | 8     |
| `Edits/`            | `EditContractTests`            | unit       | 5..13 |
| `PropertyRules/`    | `TransformRulesTests`          | unit       | 8     |
|                     | `PerspectiveCameraRulesTests`  | unit       | 8     |
|                     | `OrthographicCameraRulesTests` | unit       | 8     |
|                     | `DirectionalLightRulesTests`   | unit       | 8     |
|                     | `LocalLightRulesTests`         | unit       | 8     |
|                     | `LocalFogVolumeRulesTests`     | unit       | 8     |
|                     | `NodeRenderingRulesTests`      | unit       | 8     |
|                     | `SceneEnvironmentRulesTests`   | unit       | 8     |
| `Edits/Properties/` | `SetPropertiesTests`           | unit       | 8     |
|                     | `SetScenePropertiesTests`      | unit       | 8     |
| `Edits/Components/` | `AddComponentTests`            | unit       | 9     |
|                     | `RemoveComponentTests`         | unit       | 9     |
| `Edits/Layout/`     | `CreateFolderTests`            | unit       | 10    |
|                     | `RemoveFolderTests`            | unit       | 10    |
|                     | `RestoreFolderTests`           | unit       | 10    |
|                     | `RenameFolderTests`            | unit       | 10    |
|                     | `MoveLayoutEntriesTests`       | unit       | 10    |
|                     | `LayoutScopesTests`            | unit       | 10    |
| `Edits/Hierarchy/`  | `InsertNodesTests`             | unit       | 11    |
|                     | `RemoveNodesTests`             | unit       | 11    |
|                     | `RenameNodeTests`              | unit       | 11    |
|                     | `ReparentNodesTests`           | unit       | 11    |
|                     | `ReorderNodeTests`             | unit       | 11    |
|                     | `PlaceNodesTests`              | unit       | 11    |
| `Nodes/`            | `SceneComponentsTests`         | unit       | 9     |
|                     | `SceneNodeTemplatesTests`      | unit       | 11    |
|                     | `SceneNodeCopiesTests`         | unit       | 12    |
|                     | `SceneClipboardTests`          | unit       | 12    |
| `Edits/Assets/`     | `SetGeometryTests`             | unit       | 13    |
|                     | `SetMaterialSlotTests`         | unit       | 13    |
|                     | `MaterialSlotInventoryTests`   | unit       | 13    |
|                     | `RemoveMaterialSlotsTests`     | unit       | 13    |
| `Edits/Settings/`   | `SetScenePlayTests`            | unit       | 7     |
|                     | `SetSceneReferencesTests`      | unit       | 7     |
|                     | `SetScriptSlotsTests`          | unit       | 7     |
| `Edits/EditorView/` | `SetEditorHiddenTests`         | unit       | 7     |
| `Documents/`        | `SceneContentTests`            | functional | 5     |
|                     | `SceneRelocationTests`         | functional | 6     |
| `Scenarios/`        | `HierarchyScenarioTests`       | functional | 12    |
|                     | `PropertyScenarioTests`        | functional | 8     |

`EditContractTests` gains the rows of each edit when its task lands. A scenario
that needs an edit of a later task lands with that task.

### 2.2 Test support (`tests/TestSupport`)

Compiled only by this project, never linked from another module's tests; other
modules' tests build their own data and fake Scenes' public ports.

A type goes in `TestSupport/` only when two or more test classes use it. Data
used by one class stays in that class (the rule rows) or next to it
(`Edits/EditSamples.cs` beside `EditContractTests`).

| Type                       | Used by                                    |
| -------------------------- | ------------------------------------------ |
| `SampleScene`              | almost every class (3.1)                   |
| `SceneBuilder`             | `SampleScene`, tests needing another shape |
| `AuthoringFixture`         | every class that executes edits            |
| `ChangeRecorder`           | the fixture                                |
| `SceneAssert`              | every class                                |
| `PropertyCase`             | the eight rule classes                     |
| `ProbeEdit`                | the `Authoring/` classes                   |
| `FakeNodeInteraction`      | the fixture; lock, hide and frame tests    |
| `FakeAssetRedirects`       | the fixture; contract, insert, relocation  |
| `FakeMaterialSlotProvider` | material slot classes                      |
| `SingleThreadContext`      | threading, change stream and gesture tests |
| `SceneSnapshot`            | `SceneAssert.Unchanged`, round trips       |
| `SceneDocuments`           | `Documents/` classes, save scenarios       |
| `MemoryFileStore`          | `SceneDocuments`: files in memory          |
| `LayoutQuery`              | layout and hierarchy classes               |

The types are code, in `tests/TestSupport`; their XML docs are the contract.
`TestSupportTests` checks the guarantees every other class relies on: the sample
decodes into the scene of 3.1 and builds the same document each time, `Without`
drops a subtree with its layout, a round trip restores the document at each
step, a thread fixture runs edits on its thread, and the fakes' gates and
redirects behave.

- `EditSample` (in `Edits/`):
  `record EditSample(string Name, Func<SceneData, ISceneEdit> Build, IReadOnlyList<SceneChange> Expected)`;
  flags `HasTargets`, `CarriesAssetReference`. `EditSamples.All`: one sample per
  public edit type, plus a batch and a nested batch, all over `SampleScene`.

## 3. How the tests are written

### 3.1 The sample scene

`SampleScene.Build()` returns this scene; tests name its nodes, never build
their own unless the scenario needs another shape.

```text
nodes (scene graph)                         explorer layout
A        transform T(1,0,0)                  A
├─ A1    rotated 90° about Y, scale 2        ├─ A1
│  └─ A11                                    │  └─ A11
└─ A2                                        └─ folder AF (expanded)
B                                                └─ A2
Cam      perspective camera, near 0.1 far 100 folder F (expanded)
Ortho    orthographic camera, size 10         ├─ B
Sun      directional light, slot Primary      └─ folder G (collapsed, empty)
Moon     directional light, slot None        Cam, Ortho, Sun, Moon, Lamp,
Lamp     point light                         Spot, Fog, Mesh, Script
Spot     spot light, cones 0.4 / 0.6
Fog      local fog volume
Mesh     geometry asset:/Content/Cube.ogeo, one override slot
Script   scripting, slot S1 → script asset, node value → B
```

The scene's environment is the default `SceneEnvironmentData`. Assets used by
tests: `asset:/Content/Cube.ogeo`, `asset:/Content/Sphere.ogeo`,
`asset:/Content/Red.omat`, `asset:/Content/Move.oscript`,
`asset:/Content/Play.oimap`.

How the shared scene stays safe to share:

- **Built in code, not files.** `SampleScene` is a `SceneBuilder` program:
  typed, refactor-safe and readable in review. No JSON fixtures; the file format
  is the World serializer's to test.
- **Fixed ids.** Each node, component and folder id is a `static readonly`
  property with a fixed, readable GUID (`…0000A1` for `A1`), so `[DynamicData]`
  rows can name them at discovery time and failures name stable ids.
- **Fresh per call.** `Build()` returns new `SceneData` every time; nothing
  mutable is shared, so parallel tests cannot interfere, and a test may adjust
  its copy (`SampleScene.Builder().Without(SampleScene.B).Build()`).
- **Rows hold ids and values, never scenes.** A row's scene is built inside the
  test.
- **A second scene** (copy between scenes, CB-07, SC-05) is a small
  `SceneBuilder` scene with its own ids, so ids of the sample are absent there.
- **Changes to the sample are reviewed like code**: a new node is added only
  when several classes need it; a scenario of one class adds its node through
  `Builder()`.

### 3.2 Shape of a test

```csharp
[TestMethod]
public async Task RenameToNewName_RenamesAndRaisesNodeRenamed()
{
    using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build());

    var result = await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box"));

    SceneAssert.Applied(result);
    fixture.Scene.Node(SampleScene.B).Name.Should().Be("Box");
    SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.NodeRenamed(SampleScene.B));
}
```

- Arrange: a fixture over `SampleScene` or a `SceneBuilder` scene; locks and
  redirects set on the fixture's fakes. Act: one call. Assert: the outcome, the
  scene, the changes, the history, in that order.
- Async tests return `Task`; nothing blocks on a task.
- Property ids come from the descriptor objects in `src`
  (`TransformDescriptors.PositionX`), never from strings.
- Values compare exactly, except rotations and preserve-world transforms
  (`SceneAssert.SameRotation`, 1e-5 on positions).

### 3.3 Data-driven tests

- `[DataRow]` when a row is primitives or enums (an axis, a bool, an index); the
  row's arguments are its name.
- `[DynamicData]` over `IEnumerable<TestDataRow<T>>` when a row carries ids,
  property ids, vectors or edits; each row sets `DisplayName`. The source is a
  static property next to the test, or `EditSamples`/`SampleScene` when shared.
- Every rule class has the same pair: `Value_InRange_Applies` and
  `Value_OutOfRange_IsInvalid` over its `InRange`/`OutOfRange` `PropertyCase`
  rows (the boundary value, and the first value past it), plus a named test for
  each cross-field rule.
- `EditContractTests` runs every frame-level obligation once per edit type over
  `EditSamples.All`; a new edit gets those tests by adding its sample, and
  `EverySceneEditType_HasASample` fails until it does.
- Per-edit classes test only what is specific to the edit; they do not repeat
  the contract rows.

### 3.4 Model classes

Four classes are written; each is the pattern for its kind. Copy the closest
one.

| Kind                    | Model                  | Shows                                                                                                          |
| ----------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------------- |
| One edit's own behavior | `RenameNodeTests`      | arrange, act, assert in that order; `[DataRow]` with display names; `Invalid` with code and node; `Unchanged`  |
| Component rules         | `LocalLightRulesTests` | `InRange`/`OutOfRange` `PropertyCase` rows from descriptors; one named test per cross-field rule; a round trip |
| The frame               | `SceneAuthoringTests`  | `ProbeEdit` set per test; asserting the probe's call log, the history and the change stream; faulted tasks     |
| Threads and asynchrony  | `SceneThreadTests`     | `CreateOnThreadAsync`; calling from the pool with `Task.Run`; thread ids from `ProbeLog` and `ChangeRecorder`  |

Rules the models follow:

- A test asserts the contract in scene-edits.md and this catalogue, never what
  the code happens to do. A test that fails because the code breaks the contract
  stays failing and the defect is reported.
- `using var fixture`; every `Should()` chain is discarded (`_ = …`), as
  everywhere in the repository.
- Diagnostics through `SceneAssert` only; no test reads a `Message`.
- No sleeps or timeouts: threads are synchronized by awaiting the frame.

### 3.5 Test doubles: hand-rolled or Moq

Real objects first: `Scene`, `SceneAuthoring`, `HistoryKeeper`, the edits, the
descriptors and the rules are never doubled. A double replaces only a port the
module consumes, and the choice follows what the test needs from it.

**Hand-rolled** when the double keeps state that tests change between calls,
when its members must agree with each other, or when tests drive it over time.
Each is a small public class in `TestSupport/` with its own state and no
assertions of its own.

- `FakeNodeInteraction` (`ISceneNodeInteraction`): locks and hidden flags in two
  sets. `SetHidden` from `SetEditorHidden` must be read back by `IsHidden` after
  an undo; `GetLockOwner` must agree with `IsLocked` walking the parents. A mock
  would make every test restate that consistency. `Lock(id)`, `Hide(id)`.
- `FakeAssetRedirects` (`IAssetRedirects`): a rename map and a deleted set. Both
  `Resolve` overloads and `WasDeleted` must agree, and tests change the map
  after an edit executed (EC-12). `Move(from, to)`, `Delete(uri)`.
- `FakeMaterialSlotProvider` (`IGeometryMaterialSlotProvider`): slot metadata
  per geometry URI, or unavailable; `HoldReads()` returns a gate that keeps
  `ReadAsync` pending until released, so a test changes the scene during the
  read (MI-03). `Publish`, `Remove`, `Fail`.
- `ProbeEdit` (`ISceneEdit`): the frame's own seam. Its validation, apply
  result, changes, revert and exception are set per test, and it records each
  call with its thread and order (BA-05, TH-03). A mock with callbacks would
  hide that record in setup code.
- `SingleThreadContext`: not a double; a real `SynchronizationContext` pumped by
  one thread.

**Moq** (`Mock.Of<T>(…)`) when the port is a precondition that returns a fixed
value or only has to exist, and the test never reads it back:

- `IProject`: stored by the scene, never called by Scenes.
- `IProjectManagerService`: `CurrentProject` returns that project.
- `ISceneDocumentCommandService`: required by `SceneDocumentKind` until task 14,
  never called by the new code.

Never `Verify`: tests assert the scene, the result, the changes and the history,
not which port methods ran. A test that needs to know a port was used reads the
hand-rolled fake's state instead. When a mocked port needs more than one fixed
return value across a test, it becomes a hand-rolled fake.

## 4. Authoring

### `SceneAuthoringTests` — the frame (3.1, 3.3)

ProbeEdit on `SampleScene` unless a row says otherwise.

- **FR-01** `Execute_ValidEdit_AppliesRecordsOneEntryAndRaisesItsChanges` →
  `Applied`; undo count 1, entry label = edit label; one `Changed` carrying the
  probe's changes and the fixture's `DocumentId`.
- **FR-02** `Execute_AfterDispose_IsRetiredWithoutValidating` → `Retired`
  [`Retired`]; probe never called.
- **FR-03** `Execute_MissingTarget_IsRefusedWithoutValidating` → target is a new
  `NodeId`: `Refused` [`TargetMissing`, that id]; probe never called.
- **FR-04** `Execute_LockedTarget_IsRefused` → `B` locked: [`TargetLocked`, B].
- **FR-05** `Execute_TargetUnderLockedAncestor_IsRefusedForTheTarget` → `A`
  locked, target `A11`: [`TargetLocked`, A11].
- **FR-06** `Execute_TwoBadTargets_ReportsOneErrorEach` → missing id and locked
  `B`: two diagnostics, one per node.
- **FR-07** `Execute_LockedTargetWithoutInteractionPort_Applies` → fixture built
  without the port.
- **FR-08** `Execute_ValidationFails_IsInvalidWithoutApplying` → probe validates
  `Invalid` [`PropertyOutOfRange`]; Apply never called; Unchanged.
- **FR-09** `Execute_ValidationWarns_AppliesAndReturnsTheWarning` → probe warns
  [`AssetReferenceDeleted`]; `Applied` with that warning.
- **FR-10** `Execute_ApplyThrows_FaultsWithTheExceptionAndRecordsNothing` →
  awaited task throws the probe's exception; no entry; no `Changed`.
- **FR-11** `Execute_ApplyReturnsNoChanges_AppliesWithoutRecordingOrRaising` →
  `Applied`; no entry; no `Changed`; document clean (3.2).
- **FR-12** `Execute_EditNotChangingTheDocument_RecordsAndKeepsTheDocumentClean`
  → `ChangesDocument = false`: one entry; `History` reports no unsaved change.
- **FR-13** `Validate_ValidEdit_ChangesNothing` → Unchanged; Apply never called.
- **FR-14** `Validate_LockedTarget_IsRefused` [`TargetLocked`].
- **FR-15** `Validate_AfterDispose_IsRefusedAsRetired` → `Refused` [`Retired`]
  (3.3 step 1).
- **FR-16** `Validate_WarningThenExecuteWithout_ReturnsNoWarning` → a validate
  that warns, then an execute of a probe that does not: no warning.
- **FR-17** `Execute_InvalidThenValid_ReturnsNoStaleWarning` → an invalid probe
  that warned before failing, then a valid one: the second has no warning.
- **FR-18** `Execute_NullEdit_ThrowsArgumentNull`; **FR-19**
  `Validate_NullEdit_ThrowsArgumentNull`.
- **FR-20** `Dispose_Twice_DoesNotThrow`.
- **FR-21** `Decode_ExposesTheDecodedSceneAndTheContextId` → `Scene` has the
  sample's node ids; `Document` = the context's id.

### `UndoRedoTests` (3.4)

Real edits on `SampleScene`: `RenameNode(B, "Box")` unless stated.

- **UR-01** `Undo_AfterRename_RestoresTheNameAndRaisesNodeRenamed` → true.
- **UR-02** `Redo_AfterUndo_RenamesAgain` → true.
- **UR-03** `Undo_EmptyHistory_IsNothingToEdit` → `Invalid` [`NothingToEdit`].
- **UR-04** `Redo_NothingUndone_IsNothingToEdit` → `Invalid` [`NothingToEdit`].
- **UR-05** `Undo_TargetLockedSinceTheEdit_StillUndoes` → lock `B` after the
  rename; undo true; redo true.
- **UR-06** `Undo_TargetRemovedOutsideTheHistory_IsRefusedAndKeepsTheEntry` →
  rename `B`; remove `B` through a `ChangesDocument = false` probe that does not
  record; undo `Refused` [`TargetMissing`, B]; undo count still 1; Unchanged.
- **UR-07** `Undo_BatchWhoseRevertNoLongerFits_IsRefusedAndKeepsTheEntry` →
  batch rename `B` + rename `A`; `A` removed outside the history; undo `Refused`
  [`TargetMissing`, A]; `B` keeps "Box".
- **UR-08** `Undo_AfterDispose_IsRetiredAndKeepsTheEntry` [`Retired`].
- **UR-09** `Execute_AfterUndo_ClearsRedo` → redo `Invalid` [`NothingToEdit`].
- **UR-10** `UndoAllThenRedoAll_ReturnsToEachStateInOrder` → five edits (rename,
  reparent, create folder, set position, hide); snapshot after each undo and
  redo equals the recorded state.
- **UR-11** `Undo_HideInEditor_KeepsTheDocumentClean`.
- **UR-12** `Undo_MovesTheEntryToRedo` → undo count 0, redo count 1.

### `SceneEditBatchTests` (3.5)

- **BA-01** `TwoValidEdits_ApplyInOrderAsOneEntry` →
  `[RenameNode(A,"X"), RenameNode(B,"Y")]`: one entry with the batch label; one
  `Changed`: `NodeRenamed(A)`, `NodeRenamed(B)`.
- **BA-02** `InsertThenRenameTheInsertedNode_Applies` → the rename's target
  exists only after the insert.
- **BA-03** `SecondEditInvalid_RollsBackTheFirst` → rename `A`, then rename `B`
  to "": `Invalid` [`NameEmpty`, B]; Unchanged.
- **BA-04** `SecondEditTargetLocked_RollsBackTheFirst` → `Refused`
  [`TargetLocked`, B]; Unchanged.
- **BA-05** `ThirdOfThreeFails_RevertsTheFirstTwoInReverseOrder` → probes record
  reverts: second, then first.
- **BA-06** `RevertRejectedDuringRollback_Throws` → a probe whose revert is
  `Rejected`: `InvalidOperationException`.
- **BA-07** `Undo_RestoresTheSceneBeforeTheBatch`.
- **BA-08** `Redo_ReappliesEveryInnerEdit`.
- **BA-09** `Empty_IsInvalid` [`BatchEmpty`] (3.5).
- **BA-10** `InnerEditsChangingNothing_RecordNothing` → rename `A` to its own
  name, hide a hidden node: `Applied`, no entry (3.2).
- **BA-11** `NestedBatchFailing_RollsBackTheOuterBatch` → outer: rename `A`,
  inner: [rename `B`, rename missing id]: [`TargetMissing`]; Unchanged.
- **BA-12** `ChangesDocument` — `[DataRow]`: `AllHide` → false; `HideAndRename`
  → true.
- **BA-13** `Validate_ValidBatch_ChangesNothing` → Unchanged.
- **BA-14** `Validate_SecondEditInvalid_ReturnsItsDiagnosticAndChangesNothing`.
- **BA-15** `Validate_FolderEdits_DoNotTouchTheLiveLayout` → batch removing
  folder `F` and collapsing `AF`; the live layout keeps `F`, `IsExpanded` of
  `AF` stays true.
- **BA-16** `InnerWarnings_AreReturned` → insert with a deleted asset: `Applied`
  with [`AssetReferenceDeleted`, the inserted node] (EC-13: the warning is on
  the node).

### `ChangeStreamTests` (3.7)

- **CS-01** `OneEdit_RaisesOneEvent`.
- **CS-02** `UndoAndRedo_RaiseOneEventEach`.
- **CS-03** `Handler_ReadsTheNewValueFromTheScene` → in the `NodeRenamed`
  handler, `B` already has the new name.
- **CS-04** `ThreeEdits_RaiseInExecutionOrder`.
- **CS-05** `RefusedEdit_RaisesNothing`.
- **CS-06** `Event_CarriesTheDocumentId`.

What each edit raises is `EditContractTests.Apply_RaisesTheDeclaredChanges`.

### `SceneThreadTests` — rule 4 (3.1)

Fixture from `CreateOnThreadAsync`.

- **TH-01** `Execute_FromThreadPool_RunsAndRaisesOnTheSceneThread` → probe's
  Validate, Apply and the `Changed` handler all on the context thread.
- **TH-02** `Operation_FromThreadPool_RunsOnTheSceneThread` → `[DataRow]`:
  `Undo`, `Redo`, `Validate`; the probe's calls run on the context thread.
  `BeginGesture` has no probe call to observe; `SceneGestureTests` covers it.
- **TH-03** `FiftyExecutes_FromParallelThreads_NeverOverlap` → probes record
  enter/exit; no overlap; 50 entries.
- **TH-04** `Execute_OnTheSceneThread_CompletesSynchronously` → returned task
  already completed.
- **TH-05** `Execute_ApplyThrowsOnTheSceneThread_FaultsTheCallersTask`.
- **TH-06** `NoContextAtDecode_RunsOnTheCallersThread` → fixture from
  `CreateAsync` on a pool thread with no context: probe runs on the caller.

### `SceneGestureTests` (3.6)

Gesture on `A`'s `Transform.PositionX` unless stated; `A` starts at x = 1.

A refused gesture call is `Refused`. A call that finds its gesture cancelled
(GE-13, GE-14, GE-22) is `Refused` [`GestureCancelled`] naming no node: the
gesture, not one node, ended. `BeginGestureAsync` reports a refusal by throwing
`SceneEditRefusedException`, whose `Result` carries the diagnostic.

- **GE-01** `Begin_AppliesAndRaisesNothing`.
- **GE-02** `Begin_LockedTarget_IsRefused` [`TargetLocked`, A].
- **GE-03** `Begin_MissingTarget_IsRefused` [`TargetMissing`].
- **GE-04** `Preview_WritesTheValueAndRaisesPreview` → x = 5;
  `PropertiesChanged(A, [PositionX], Preview)`; no entry.
- **GE-05** `Preview_OtherTargetOrProperty_IsRefused` — `[DataRow]`: `OtherNode`
  (B), `OtherProperty` (PositionY) → [`GestureMismatch`]; x unchanged.
- **GE-06** `Preview_InvalidValue_KeepsTheLastValidPreview` → preview 5, then
  NaN: [`PropertyOutOfRange`, A, PositionX]; x = 5.
- **GE-07** `Commit_AfterHundredPreviews_RecordsOneEntryFromStartToLast` → x
  1..100; one entry; undo x = 1; redo x = 100.
- **GE-08** `Commit_RaisesCommit`.
- **GE-09** `Commit_BackAtTheStartValue_RecordsNothing` → previews 5 then 1.
- **GE-10** `Commit_WithoutPreview_RecordsNothing`.
- **GE-11** `Cancel_RestoresTheStartValueAndRaisesCancel` → no entry.
- **GE-12** `Call_AfterTheGestureEnded_IsRefused` — `[DataRow]`:
  `PreviewAfterCommit`, `CommitAfterCommit`, `PreviewAfterCancel` →
  [`GestureMismatch`]; nothing changes.
- **GE-13** `Preview_AfterTargetRemoved_CancelsTheGesture` → remove `A` by an
  edit; next preview [`GestureCancelled`]; no entry; `A` stays removed.
- **GE-14** `Commit_AfterComponentRemovedAndRestored_CancelsTheGesture` →
  gesture on `Lamp` Range; another caller removes the light and undoes the
  removal (a new component object): commit [`GestureCancelled`]; no entry.
- **GE-15** `Commit_RotationAcrossGimbalLock_UndoRestoresTheOrientation` →
  gesture on `A1` `RotationX` from (90, 25, 0); undo: `SameRotation` with the
  start.
- **GE-16** `HasEditsInProgress` — `[DataRow]`: `AfterBegin` → false;
  `AfterPreview` → true; `AfterPreviewBackToStart` → false; `AfterCommit` →
  false.
- **GE-17** `FinishEdits_CommitsEveryOpenGesture` → gestures on `A` and `B`: two
  entries; `HasEditsInProgress` false.
- **GE-18** `Dispose_CancelsOpenGestures` → x back to 1; no entry.
- **GE-19** `SceneProperties_PreviewAndCommit_RecordOneEntry` → empty targets,
  `ExposureCompensation`; `PropertiesChanged(null, …)`.
- **GE-20** `EditOnAnotherNodeDuringGesture_LeavesTheGestureOpen` → rename `B`
  between previews; commit records the gesture.
- **GE-21** `OneShotEditOfTheSameNodeDuringGesture_AppliesAndTheGestureGoesOn` →
  set `A` PositionY between previews; next preview accepted (3.6).
- **GE-22** `Preview_AfterUndoRemovedTheTargetComponent_CancelsTheGesture` →
  point light added to `B`, gesture on its Range, undo of the add: next preview
  [`GestureCancelled`] (3.6).

## 5. `EditContractTests` — every edit (3.2, 3.3, 3.11, rule 1)

`[DynamicData]` over `EditSamples.All` (one row per edit type, display name =
the edit type) unless stated.

- **EC-01** `RoundTrip_RestoresTheSceneExactly`.
- **EC-02** `Apply_RaisesTheDeclaredChanges` → the sample's `Expected`, in order
  (3.7).
- **EC-03** `Undo_RaisesTheChangesOfTheRevert` → the changes that put the scene
  back.
- **EC-04** `MissingTarget_IsRefused` → rows: samples with `HasTargets`; first
  target removed from the scene data before decode: [`TargetMissing`].
- **EC-05** `LockedTarget_IsRefused` → rows: `HasTargets` except
  `SetEditorHidden`: [`TargetLocked`].
- **EC-06** `TargetUnderLockedAncestor_IsRefused` → as EC-05 with the parent
  locked.
- **EC-07** `AfterDispose_IsRetired` [`Retired`].
- **EC-08** `Validate_ChangesNothing`.
- **EC-09** `Json_RoundTripsTheEdit` → `SceneEditJson.Options`, read and written
  as `ISceneEdit`: the copy has the edit's type, re-serializes to the same JSON
  and executes to the same `Dehydrate()` (record equality compares lists by
  reference, so it is not the test).
- **EC-10** `Json_RoundTripsTheRevert` → the recorded revert, through
  `SceneEditJson.Options` as `ISceneEdit`, deserializes and undoes.
- **EC-11** `ConstructorInput_ChangedAfterwards_DoesNotChangeTheEdit` → rows:
  samples built from serialization records or `PropertyEdit`; mutate the input
  lists; the edit applies the original.
- **EC-12** `Undo_AssetRenamedAfterTheEdit_RestoresTheNewUri` → rows:
  `CarriesAssetReference`; rename the asset in `Redirects` after execute; undo
  then redo: the scene holds the new URI.
- **EC-13** `Redo_AssetDeletedAfterTheEdit_RestoresWithAWarning` → same rows;
  execute, undo, delete the asset, redo: `Applied` with
  [`AssetReferenceDeleted`, the node holding the reference, none for scene
  references]; the reference is kept (3.4, 3.11).
- **EC-14** `EditType_HasOnlyAllowedMembers` → `[DynamicData]` over every public
  `ISceneEdit` type in the assembly: no member of type `Scene`, `SceneNode`,
  `GameComponent`, `Guid`, `List<>` or array.
- **EC-15** `EverySceneEditType_HasASample` → no public `ISceneEdit` type
  without a row in `EditSamples.All`.

## 6. Properties

### `TransformRulesTests`

Node `A1`.

- **TR-01** `Value_InRange_Applies` → rows: `PositionX_Large` (1e6),
  `PositionY_Negative` (−1e6), `RotationZ_FullTurn` (360), `ScaleX_Negative`
  (−2), `ScaleY_Tiny` (1e-4).
- **TR-02** `Value_OutOfRange_IsInvalid` → [`PropertyOutOfRange`, A1, the row's
  property]; rows: `PositionX_NaN`, `PositionZ_Infinity`, `RotationY_NaN`,
  `ScaleX_Zero`, `ScaleZ_Infinity`.
- **TR-03** `RotationAxis_Undo_RestoresTheWholeOrientation` → `[DataRow]` X, Y,
  Z: from (90, 25, 0) set the axis to 15; undo `SameRotation` with the start;
  redo `SameRotation` with the edited orientation.
- **TR-04** `RotationAxis_Edit_RaisesAllThreeAxes` → `[DataRow]` X, Y, Z on `A1`
  at (90, 25, 0): `PropertiesChanged` lists RotationX, RotationY, RotationZ; the
  axes are coupled (3.10), and at gimbal lock an edit of one moves the reading
  of another.

### `PerspectiveCameraRulesTests`

Node `Cam` (near 0.1, far 100).

- **CA-01** `Value_InRange_Applies` → rows: `FieldOfView_1`, `FieldOfView_179`,
  `NearPlane_Tiny` (1e-4), `FarPlane_Large` (1e6), `AspectRatio_Small` (0.01),
  `ApertureF_Small`, `ShutterRate_Small`, `Iso_Small`.
- **CA-02** `Value_OutOfRange_IsInvalid` → rows: `FieldOfView_BelowOne` (0.99),
  `FieldOfView_Above179` (179.01), `FieldOfView_NaN`, `NearPlane_Zero`,
  `NearPlane_Negative`, `FarPlane_Infinity`, `AspectRatio_Zero`,
  `ApertureF_Zero`, `ShutterRate_Zero`, `Iso_Zero`, `AspectMode_Undefined`.
- **CA-03** `NearNotBelowFar_IsInconsistent` → [`PropertiesInconsistent`, Cam]
  (a cross-field rule names no property, 3.8); rows: `NearEqualsFar` (near 100),
  `NearAboveFar` (near 200), `FarEqualsNear` (far 0.1), `FarBelowNear` (far
  0.05).
- **CA-04** `NearAndFarTogether_ValidatesTheResult` → near 150 and far 300 in
  one edit: applies.
- **CA-05** `TwoCamerasOneInvalid_ChangesNeither` → `Cam` and a second camera
  with far 5, near set to 10 on both: [`PropertiesInconsistent`, the second];
  Unchanged.

### `OrthographicCameraRulesTests`

Node `Ortho`.

- **OC-01** `Value_InRange_Applies` → rows: `Size_Tiny` (1e-4),
  `NearPlane_Tiny`, `AspectRatio_Small`.
- **OC-02** `Value_OutOfRange_IsInvalid` → rows: `Size_Zero`, `Size_Negative`,
  `NearPlane_Zero`, `AspectRatio_Zero`, `ApertureF_Zero`, `Size_NaN`.
- **OC-03** `NearNotBelowFar_IsInconsistent` → rows: `NearEqualsFar`,
  `FarBelowNear`.

### `DirectionalLightRulesTests`

Nodes `Sun` (slot Primary) and `Moon` (slot None).

- **DL-01** `Value_InRange_Applies` → rows: `CascadeCount_1`, `CascadeCount_4`,
  `DistributionExponent_1`, `TransitionFraction_0`, `TransitionFraction_1`,
  `AngularSize_0`, `AngularSize_Pi`, `Color_Black`, `DiskScale_Zero`,
  `IntensityLux_Zero`.
- **DL-02** `Value_OutOfRange_IsInvalid` → rows: `CascadeCount_0`,
  `CascadeCount_5`, `DistributionExponent_BelowOne` (0.99),
  `TransitionFraction_Negative`, `DistanceFadeoutFraction_AboveOne`,
  `MaxShadowDistance_Zero`, `AngularSize_AbovePi`, `Color_NegativeRed`,
  `ExposureCompensation_NaN`, `ShadowBias_Negative`,
  `ShadowNormalBias_Negative`, `DiskScale_Negative`, `IntensityLux_Negative`,
  `ShadowResolutionHint_Undefined`, `SplitMode_Undefined`,
  `AtmosphereSlot_Undefined`.
- **DL-03** `ActiveCascadeDistancesNotIncreasing_IsInconsistent` →
  [`PropertiesInconsistent`]; rows: `TwoCascades_EqualDistances`,
  `FourCascades_ThirdAboveFourth`.
- **DL-04** `InactiveCascadeDistanceOutOfOrder_Applies` → count 2, distance 3
  below distance 2.
- **DL-05** `IntensityWithBreakingCascadeDistance_ChangesNeither` → intensity
  and distance 0 = 500 in one edit: [`PropertiesInconsistent`]; Unchanged.
- **DL-06** `AtmosphereSlotOwnedByAnotherLight_IsInvalid` → `Moon` to Primary:
  [`AtmosphereSlotOccupied`, Moon]; Unchanged.
- **DL-07** `ClearOwnerThenAssign_InOneBatch_MovesTheSun` → batch: `Sun` None,
  `Moon` Primary: one entry; undo: `Sun` Primary, `Moon` None (4).
- **DL-08** `AssignThenClearOwner_InOneBatch_IsInvalid` →
  [`AtmosphereSlotOccupied`, Moon]; Unchanged (4).
- **DL-09** `OtherPropertyOfTheSun_KeepsItsSlot` → `Sun` IntensityLux 5.
- **DL-10** `ColorChannel_Edit_KeepsTheOtherChannels` → `[DataRow]` R, G, B;
  undo restores all three.
- **DL-11** `CascadeDistance_Edit_ChangesOnlyThatDistance` → `[DataRow]` 0..3;
  undo restores all four.

### `LocalLightRulesTests`

Nodes `Lamp` (point) and `Spot` (cones 0.4 / 0.6).

- **LL-01** `Value_InRange_Applies` → rows: `Range_Tiny`, `Flux_Zero`,
  `SourceRadius_Zero`, `Color_Black`, `OuterCone_RightAngle` (π/2, inner 0.4).
- **LL-02** `Value_OutOfRange_IsInvalid` → rows: `Range_Zero`, `Range_Negative`,
  `Flux_Negative`, `SourceRadius_Negative`, `ShadowBias_Negative`,
  `Color_NegativeGreen`, `ExposureCompensation_Infinity`, `InnerCone_Negative`,
  `OuterCone_AboveRightAngle`, `ShadowResolutionHint_Undefined`.
- **LL-03** `InnerConeAboveOuter_IsInconsistent` → inner 0.7:
  [`PropertiesInconsistent`, Spot]; a cross-field rule names the node, not a
  property.
- **LL-04** `BothConesAtRightAngle_IsInconsistent` → [`PropertiesInconsistent`,
  Spot].
- **LL-05** `RangeAndFlux_RoundTrip` → `Lamp` range 20, flux 1600.

### `LocalFogVolumeRulesTests`

Node `Fog`.

- **FV-01** `Value_InRange_Applies` → rows: `RadialExtinction_Zero`,
  `HeightFalloff_Zero`, `HeightOffset_Negative`, `PhaseG_Max` (0.999),
  `Albedo_One`, `Emissive_Zero`, `SortPriority_Min` (−127), `SortPriority_Max`
  (127).
- **FV-02** `Value_OutOfRange_IsInvalid` → rows: `RadialExtinction_Negative`,
  `PhaseG_One`, `Albedo_AboveOne`, `Emissive_Negative`, `SortPriority_BelowMin`,
  `SortPriority_AboveMax`, `HeightOffset_NaN`.

### `NodeRenderingRulesTests`

- **NR-01** `Flag_Toggle_RoundTrips` → `[DataRow]` `IsVisible`, `CastsShadows`,
  `ReceivesShadows` on `Mesh`.

### `SceneEnvironmentRulesTests`

Through `SetSceneProperties`; the sample's environment.

- **EN-01** `Value_InRange_Applies` → rows: `PlanetRadius_One`,
  `GroundAlbedo_Zero`, `GroundAlbedo_One`, `MieAnisotropy_Min` (−0.999),
  `MieAnisotropy_Max` (0.999), `AerialStart_SchemaMinimum`,
  `BlackInfluence_One`, `FogMaxOpacity_One`, `FogDirectionalExponent_Min`
  (1e-6), `FogDirectionalExponent_Max` (1000), `FogEnd_BelowStart`,
  `SkyLightLowerHemisphereBlend_One`.
- **EN-02** `Value_OutOfRange_IsInvalid` → rows: `ExposureMode_Undefined`,
  `ToneMapping_Undefined`, `ManualExposure_NaN`,
  `ExposureCompensation_Infinity`, `PlanetRadius_BelowOne`,
  `AtmosphereHeight_BelowOne`, `GroundAlbedo_AboveOne`,
  `RayleighHeight_BelowOne`, `MieAnisotropy_One`, `SkyLuminance_Negative`,
  `AerialDistanceScale_Negative`, `AerialStart_BelowSchemaMinimum`,
  `BlackInfluence_AboveOne`, `TransitionDistance_Zero`,
  `MeteringMask_NotAnAssetUri`, `Backdrop_None`, `FogDensity_Negative`,
  `FogMaxOpacity_AboveOne`, `FogDirectionalExponent_Zero`,
  `FogVolumetricAlbedo_AboveOne`, `SkyIntensity_Negative`,
  `SkyLightLowerHemisphereBlend_AboveOne`.
- **EN-03** `CompensationCurve` → rows: `SixtyFiveKeys`, `RepeatedEv` →
  [`PropertyOutOfRange`, the curve]: one property's value (3.8).
- **EN-04** `CubemapUri_NotAnAssetUri_IsInvalid` → [`PropertyOutOfRange`, the
  cubemap]; rows: `FileUri`, `WithQuery`, `WithFragment`. A property value is
  checked by its descriptor (3.8); `AssetReferenceInvalid` is for edits whose
  own fields are asset references (geometry, material slot, script slot).
- **EN-05** `BackgroundColorAboveOne_WhileBackgroundShown_IsInconsistent` →
  [`PropertiesInconsistent`].
- **EN-06** `GroupProperty_Undo_RestoresTheWholeGroup` → `[DataRow]`
  `PostProcess`, `SkyAtmosphere`, `Fog`, `SkySphere`, `SkyLight`.
- **EN-07** `Backdrop_Change_UndoRestoresTheCoupledFlags` → `[DynamicData]`
  from/to over Atmosphere, Cubemap, SolidColor (six rows, e.g.
  `AtmosphereToCubemap`).
- **EN-08** `SolidColorLightsScene_Toggle_UndoRestoresWhereTheColorLives` →
  `[DataRow]` true, false.

### `SetPropertiesTests` (3.10)

- **PR-01** `OneNode_WritesTheValuesAndRaisesCommit` → `A` PositionX 3:
  `PropertiesChanged(A, [PositionX], Commit)`.
- **PR-02** `TwoNodes_RecordOneEntryAndRaiseOneChangeEach` → `A`, `B`.
- **PR-03** `Undo_RestoresTheBeforeValuesOfEachNode`.
- **PR-04** `ValuesEqualTheCurrent_AppliesWithoutAnEntry` (3.2).
- **PR-05** `PropertyOfAnotherKind_IsInvalid` → a scene environment property in
  a node edit on `A`: [`PropertyUnknown`] (a node component property on a node
  without that component is PR-07).
- **PR-06** `MixedComponentKinds_IsInvalid` [`PropertyUnknown`].
- **PR-07** `NodeWithoutTheComponent_IsInvalid` → camera property on `B`:
  [`ComponentMissing`, B].
- **PR-08** `OneNodeBreaksARule_ChangesNoNode` → `Cam` and a far-5 camera.
- **PR-09** `EmptyEdits_IsInvalid` [`NothingToEdit`].

### `SetScenePropertiesTests` (3.10)

- **SP-01** `ExposureCompensation_RaisesSceneChange` →
  `PropertiesChanged(null, …, Commit)`.
- **SP-02** `NodeComponentProperty_IsInvalid` [`PropertyUnknown`].
- **SP-03** `Undo_RestoresTheEnvironment`.

## 7. Components

### `AddComponentTests` (3.10.1)

- **AC-01** `PointLightToB_AddsAtTheIndexAndRaisesComponentAdded`.
- **AC-02** `AtIndex_InsertsThere` → point light at index 1 on `Mesh` lands
  between the transform and the geometry.
- **AC-03** `IndexPastTheEnd_IsInvalid` [`PositionOutOfRange`].
- **AC-04** `SecondOfAFamily_IsInvalid` → [`ComponentDuplicate`, node]; rows:
  `TransformOnB`, `GeometryOnMesh`, `OrthographicCameraOnCam`,
  `SpotLightOnLamp`, `DirectionalLightOnSun`, `FogVolumeOnFog`,
  `ScriptingOnScript`.
- **AC-05** `ExistingComponentId_IsInvalid` [`ComponentDuplicate`].
- AC-06 (unsupported kind) has no test: every `ComponentData` the scene format
  knows is a supported kind, and the constructor copies its input through that
  format, so no unsupported kind reaches `Validate` through the public API.
- **AC-07** `DirectionalLightTakingTheSunSlot_IsInvalid` → light data with slot
  Primary on `B`: [`AtmosphereSlotOccupied`].

### `RemoveComponentTests`

- **RC-01** `PointLightFromLamp_RemovesAndRaisesComponentRemovedWithItsKind`.
- **RC-02** `Undo_RestoresTheComponentAtItsIndex` → `Mesh` geometry.
- **RC-03** `Transform_IsInvalid` [`ComponentNotRemovable`].
- **RC-04** `UnknownComponent_IsInvalid` [`ComponentMissing`].

### `SceneComponentsTests` (3.10.1)

- **CM-01** `AddDirectionalLight_OnIdentityRotation_SetsTheDefaultRotation` →
  `B`: one entry; undo removes the light and restores identity.
- **CM-02** `AddDirectionalLight_OnRotatedNode_KeepsTheRotation` → `A1`.
- **CM-03** `Add_UsesTheKindDefaults` → `[DataRow]`: `PointLight` (1600 lm,
  range 10), `SpotLight` (1600 lm, range 15), `DirectionalLight` (casts shadows,
  slot None), `Scripting` (one empty slot), `Geometry`, `PerspectiveCamera`,
  `OrthographicCamera`, `LocalFogVolume`.
- **CM-04** `AddDirectionalLight_LeavesTheSunWithItsOwner` → slot None.
- **CM-05** `UnknownKind_ThrowsArgumentException`.
- **CM-06** `KindOf_CreatedComponent_IsItsKind` → `[DataRow]` over the eight
  kinds of CM-03: `KindOf(Create(kind))` = kind.
- **CM-07** `KindOf_Transform_ThrowsArgumentException` → a transform cannot be
  added.

## 8. Layout (section 4)

### `CreateFolderTests`

- **CF-01** `At_CreatesTheFolderThere` → `[DynamicData]` rows: `RootEnd`,
  `RootStart`, `InsideF`, `InsideNestedG`, `UnderNodeA`; raises `LayoutChanged`.
- **CF-02** `Name_IsTrimmed` → " New " is "New".
- **CF-03** `BlankName_IsInvalid` → `[DataRow]` "", " ": [`NameEmpty`].
- **CF-04** `ExistingFolderId_IsInvalid` [`FolderExists`].
- **CF-05** `InMissingFolder_IsInvalid` [`FolderMissing`].
- **CF-06** `UnderMissingNode_IsRefused` [`TargetMissing`].
- **CF-07** `UnderLockedNode_IsRefused` [`TargetLocked`, A].
- **CF-08** `OnSceneWithoutLayout_SeedsItAndUndoRemovesIt` → a scene decoded
  with no layout gets one entry per node (decode seeds it); create a folder at
  the root start: the root is the folder then the seeded entries; undo restores
  the document.

### `RemoveFolderTests`

- **FD-01** `PromoteChildren_PutsThemAtTheFolderPositionInOrder` → remove `F`
  between `A` and `Cam`: `B`, `G` take its place (4).
- **FD-02** `PromoteChildren_KeepsParentsAndWorldTransforms` → `AF` under `A`:
  `A2` keeps parent `A` and its world matrix.
- **FD-03** `PromoteChildren_NestedFolderIsPromotedWhole` → `G` stays a folder.
- **FD-04** `Undo_RestoresTheFolderPositionChildrenAndExpandedState`.
- **FD-05** `FolderContainingLockedNode_IsRemoved` → `B` locked.
- **FD-06** `InLockedNodeScope_IsRefused` → `A` locked, folder `AF`:
  [`TargetLocked`, A].
- **FD-07** `MissingFolder_IsInvalid` [`FolderMissing`].
- **FD-08** `WithoutPromotion_EmptyFolder_IsRemoved` → `G`.
- **FD-09** `WithoutPromotion_NonEmptyFolder_IsInvalid` [`FolderNotEmpty`] (4).

### `RestoreFolderTests`

- **FS-01** `RemovedFolder_IsRestoredWithItsChildren` → round trip of the revert
  of FD-01.
- **FS-02** `ExistingId_IsInvalid` [`FolderExists`].

### `RenameFolderTests`

- **FN-01** `NewName_RenamesAndRaisesLayoutChanged`.
- **FN-02** `BlankName_IsInvalid` [`NameEmpty`].
- **FN-03** `MissingFolder_IsInvalid` [`FolderMissing`].
- **FN-04** `SameName_AppliesWithoutAnEntry` (3.2).

### `MoveLayoutEntriesTests`

- **ML-01** `To_MovesWithinTheScope` → `[DynamicData]` rows: `BToRootStart`,
  `CamIntoF`, `GToRoot`, `A2OutOfAF` (to `A`'s entry).
- **ML-02** `Undo_PutsEachEntryBackAtItsPosition` → `B` and `Cam` into `G`.
- **ML-03** `NodeToAnotherScope_IsInvalid` → `A2` to root:
  [`LayoutScopeMismatch`].
- **ML-04** `FolderWithNodesToAnotherScope_IsInvalid` → `AF` to root.
- **ML-05** `FolderIntoItselfOrDescendant_IsInvalid` → `[DataRow]` `FIntoF`,
  `FIntoG`: [`HierarchyCycle`].
- **ML-06** `MovedFolder_KeepsItsChildrenAndExpandedState` → `F` to the root
  start: `B` and the collapsed `G` still inside.
- **ML-07** `Index_OutsideZeroToCount_IsInvalid` → `[DataRow]` −1, count + 1:
  [`PositionOutOfRange`]; `Index_Count_Appends` → count appends (4).
- **ML-08** `NoEntries_IsInvalid` [`NothingToEdit`].
- **ML-09** `CrossScopeGroup_AsBatch_ReparentsKeepingLocal` → batch
  `ReparentNodes([Cam], A, preserveWorld: false)` + move `Cam` into `AF`: one
  entry; undo restores parent, local transform and layout.
- **ML-10** `Move_RaisesOnlyLayoutChanged`.

### `LayoutScopesTests`

- **LS-01** `ScopeOf` → `[DynamicData]` rows: `Root` → null, `FolderF` → null,
  `NestedG` → null, `NodeA` → A, `FolderAF` → A.
- **LS-02** `PositionOf_ReturnsTheEntryPosition` → `B` → (F, null, 0).
- **LS-03** `EntriesOf` → `[DynamicData]` rows: `Root` → A, F, Cam, Ortho, Sun,
  Moon, Lamp, Spot, Fog, Mesh, Script; `NodeA` → A1, AF; `FolderF` → B, G;
  `FolderG` → empty.
- **LS-04** `EntriesOf_IgnoresTheIndex` → (F, null, 7) → B, G.
- **LS-05** `EntriesOf_MissingContainer_IsEmpty` → a new `FolderId`.
- **LS-06** `EndOf` → `[DataRow]`: `NothingMoving` → (F, null, 2); `MovingB` →
  (F, null, 1); `MovingAnEntryOfAnotherContainer` (`Cam`) → (F, null, 2).
- **LS-07** `EndOf_MoveToTheEnd_Applies` →
  `MoveLayoutEntries([B], EndOf(F, [B]))`: `Applied`; F holds G, B.

## 9. Hierarchy (section 4)

### `InsertNodesTests`

- **IN-01** `At_InsertsTheNodeThere` → `[DynamicData]` rows: `RootEnd`,
  `RootStart`, `UnderA`, `InFolderF`, `InFolderAF`; raises `NodesAdded([new])`,
  `LayoutChanged`.
- **IN-02** `Subtree_InsertsAllAndRaisesOnlyTheRoot`.
- **IN-03** `ExistingId_IsInvalid` → `[DataRow]` `NodeId` [`NodeExists`],
  `ComponentId` [`ComponentDuplicate`].
- **IN-04** `UnderMissingParent_IsRefused` [`TargetMissing`].
- **IN-05** `UnderLockedParent_IsRefused` [`TargetLocked`].
- **IN-06** `DirectionalLightWithTakenSlot_IsInvalid`
  [`AtmosphereSlotOccupied`].
- **IN-07** `LayoutEntryWithFolders_IsRestoredAsGiven` → entry with a nested
  collapsed folder.
- **IN-08** `RenamedAsset_IsRetargeted` → geometry URI renamed in `Redirects`
  before execute.
- **IN-09** `PositionOutsideRange_IsInvalid` [`PositionOutOfRange`].

### `RemoveNodesTests`

- **RM-01** `A_RemovesTheSubtreeAndRaisesNodesRemoved` → `A1`, `A11`, `A2` gone;
  layout `A` entry and `AF` gone.
- **RM-02** `Undo_RestoresPositionsLayoutAndExpandedState`.
- **RM-03** `NodeAndItsDescendant_RemovesTheSubtreeOnce` → `A`, `A1`.
- **RM-04** `TwoRoots_UndoRestoresSiblingOrder` → `B`, `Cam`.
- **RM-05** `LockedDescendant_IsRefused` → `A11` locked, remove `A`:
  [`TargetLocked`, A11].
- **RM-06** `NoRoots_IsInvalid` [`NothingToEdit`].
- **RM-07** `NodesAndFolders_AsBatch_AreOneEntry` → remove `Cam`, folder `F`.

### `RenameNodeTests`

- **RN-01** `NewName_RenamesAndRaisesNodeRenamed`.
- **RN-02** `Name_IsTrimmed`.
- **RN-03** `BlankName_IsInvalid` → `[DataRow]` "", " ": [`NameEmpty`].
- **RN-04** `SameName_AppliesWithoutAnEntry` (3.2).

### `ReparentNodesTests`

- **RP-01** `KeepLocal_MovesUnderTheParent` → `B` under `A`: local unchanged;
  raises `NodesPlaced([B])`, `LayoutChanged`; no `PropertiesChanged`.
- **RP-02** `PreserveWorld_UnderRotatedScaledParent_KeepsTheWorldTransform` →
  `B` under `A1`.
- **RP-03** `PreserveWorld_IgnoringParentTransform_KeepsLocalAndWorld`.
- **RP-04** `PreserveWorld_Unrepresentable_IsInvalid` → `[DataRow]`
  `SingularParent`, `ShearedResult`: [`TransformUnrepresentable`].
- **RP-05** `IntoItselfOrDescendant_IsInvalid` → `[DataRow]` `AUnderA`,
  `AUnderA11`: [`HierarchyCycle`]; with `B` in the same edit, `B` unmoved.
- **RP-06** `MissingParent_IsRefused`; **RP-07** `MissingAfterSibling_IsRefused`
  [`TargetMissing`].
- **RP-08** `LockedParticipant_IsRefused` → `[DataRow]` `Node`, `Descendant`,
  `Parent`, `AfterSibling`: [`TargetLocked`].
- **RP-09** `AfterSibling_PlacesNodesConsecutively` → `B`, `Cam` after `A1`.
- **RP-10** `ToRoot_AddsToTheRootNodes` → `A2`.
- **RP-11** `MovedNode_KeepsItsLayoutEntryFoldersAndExpandedState` → `A` under
  `B`: `AF` stays under `A`'s entry, expanded.
- **RP-12** `Undo_RestoresParentIndexLocalTransformAndLayout` → `B`, `A2` under
  `Cam`, preserve world.

### `ReorderNodeTests`

- **RO-01** `WithinParent_MovesInGraphAndLayout` → `A2` before `A1`.
- **RO-02** `LockedDescendant_IsAllowed` → `A11` locked, reorder `A`.
- **RO-03** `LockedNodeOrParent_IsRefused` → `[DataRow]` `Node`, `Parent`.
- **RO-04** `MissingParent_IsRefused` [`TargetMissing`]; **RO-05**
  `MissingFolder_IsInvalid` [`FolderMissing`].
- **RO-06** `Index_OutsideZeroToCount_IsInvalid` → `[DataRow]` −1, count + 1
  [`PositionOutOfRange`] (4).

### `PlaceNodesTests`

- **PL-01** `States_PutNodesBack` → round trip of a recorded reparent revert.
- **PL-02** `MissingNode_IsRefused` [`TargetMissing`].
- **PL-03** `UndoOfReparent_NodeLockedSince_StillApplies`.

## 10. Nodes

### `SceneNodeTemplatesTests`

- **NT-01** `Primitive` → `[DynamicData]` over every `PrimitiveKind`: name is
  the kind; geometry `BasicShapes/{kind}`; new ids.
- **NT-02** `Primitive_UndefinedKind_ThrowsArgumentOutOfRange` (4).
- **NT-03** `Light_DefaultTransform` → `[DataRow]` `Directional` (default
  rotation), `Point` (0, 2, 0), `Spot` (0, 3, 3) pitched −45°.
- **NT-04** `Light_UndefinedKind_ThrowsArgumentOutOfRange`.
- **NT-05** `Camera` → `[DataRow]` `Perspective`, `Orthographic`: node "Camera"
  with that component.
- **NT-06** `Placement_OverridesTheDefaultPositionAndRotation_KeepsScale` → spot
  light at (5, 0, 0), rotation given.
- **NT-07** `TwoCalls_ReturnDifferentIds`.
- **NT-08** `Append_PutsTheNodeAtTheEndOfItsScope` → `[DataRow]` root, `A`,
  folder `F`.

### `SceneNodeCopiesTests`

- **NC-01** `Copy_GivesNewNodeAndComponentIdsToTheWholeSubtree` → `A`.
- **NC-02** `Copy_ScriptValueToACopiedNode_FollowsTheCopy` → copy `Script` and
  `B`: value → copy of `B`.
- **NC-03** `Copy_ScriptValueToAnUncopiedNode_IsKept` → copy `Script` only.
- **NC-04** `Copy_DirectionalLight_ClearsTheSlot` → `Sun`.
- **NC-05** `Copy_TransformGiven_AppliesToTheCopyOnly`.
- **NC-06** `Copy_NodeWithItsAncestor_CopiesOnce` → `A`, `A1`.
- **NC-07** `Copy_SourceEditedAfterwards_LeavesTheCopy`.
- **NC-08** `DuplicateInPlace_EachCopyFollowsItsSourceInItsFolder` → `B`, `A2`:
  copies right after them, in `F` and `AF`; one entry.
- **NC-09** `DuplicateInPlace_MissingNode_ReturnsNull`.

### `SceneClipboardTests`

`TryPaste` fails only for what the design lists (a cut from two scopes, a
removed cut node); cycles and locks are the built batch's to refuse.

- **CB-01** `Capture_SnapshotsRootsAndLayoutEntries` → `A`, folder `F`.
- **CB-02** `Capture_Nothing_ReturnsNull`.
- **CB-03** `Capture_UnrepresentableWorldTransform_HasNoWorldNodes`.
- **CB-04** `Capture_SourceEditedAfterwards_PastesTheCapture`.
- **CB-05** `PasteCopy_GivesFoldersNewIdsAndKeepsNamesAndExpandedState`.
- **CB-06** `PasteCopy_TransformMode` → `[DataRow]` `PreserveWorld` (world
  equals the source's), `KeepLocal` (local equals the source's) under `A1`.
- **CB-07** `PasteCopy_IntoAnotherScene_KeepsUncopiedNodeValues` → second
  fixture; `Script` value → `B` id, missing there.
- **CB-08** `PasteCut_MovesEntriesAndNodesAsOneEntry` → `B`, `Cam` into `AF`.
- **CB-09** `PasteCut_FolderAcrossScopes_KeepsLocalAndNesting` → `F` into `A`.
- **CB-10** `PasteCut_IntoOwnDescendant_IsRefused` → `[DynamicData]`
  `NodeAIntoA1`, `FolderFIntoG`: the paste builds; the batch is Invalid
  [`HierarchyCycle`], naming `A` for the node row and none for the folder row.
- **CB-11** `PasteCut_SourceRemoved_FailsToBuild` → `TryPaste` fails
  [`TargetMissing`, B].
- **CB-16** `PasteCut_SourceLocked_IsRefused` → the paste builds; the batch is
  Refused [`TargetLocked`, B].
- **CB-12** `PasteCopy_LockedSource_Pastes`.
- **CB-13** `TryPaste_NodeAndFolderOfDifferentScopes_Fails` → `A2` and folder
  `G` cut: `TryPaste` fails [`LayoutScopeMismatch`].
- **CB-14** `PasteCopy_EmptyFolder_PastesAndUndoes`.
- **CB-15** `Paste_IsOneUndoableStep` → `[DataRow]` `Copy`, `Cut` of `Cam` and
  folder `G` into `AF`: one entry; one undo restores the document and the undo
  depth of before the paste.

## 11. Assets (task 13)

### `SetGeometryTests`

- **GM-01** `NewAsset_ReplacesTheGeometryAndRaisesGeometryChanged` → `Mesh` to
  Sphere.
- **GM-02** `NoAsset_IsInvalid` [`GeometryRequired`].
- **GM-03** `NewAsset_KeepsOverrideSlots`.
- **GM-04** `NodeWithoutGeometry_IsInvalid` [`ComponentMissing`].
- **GM-05** `SameAsset_AppliesWithoutAnEntry` (3.2).

### `SetMaterialSlotTests`

- **MS-01** `Material_AssignsTheSlotAndRaisesMaterialSlotsChanged`.
- **MS-02** `Null_RemovesTheOverride`.
- **MS-03** `NullOnClearSlot_AppliesWithoutAnEntry`.
- **MS-04** `GeometryChangedSinceRead_IsInvalid` [`MaterialSlotStale`].
- **MS-05** `NewerRevision_ReplacesTheOverrideAndUndoRestoresIt`: an assignment
  read from a newer inventory replaces the node's override and its revision;
  undo restores the old override, revision included (round trip).
- **MS-06** `InvalidMaterialUri_IsInvalid` → `[DataRow]` `NotAsset`,
  `WithQuery`, `WithFragment`: [`AssetReferenceInvalid`].
- **MS-07** `TwoNodesOneOnOtherGeometry_ChangesNeither` [`MaterialSlotStale`].

### `MaterialSlotInventoryTests`

- **MI-01** `Read_BuildsTheEditWithSlotIdsAndRevision`.
- **MI-02** `InventoryUnavailable_ReportsIt`
  [`MaterialSlotInventoryUnavailable`].
- **MI-03** `GeometryChangedDuringRead_EditIsStale` → gate held, geometry set,
  gate released: executing the edit [`MaterialSlotStale`].

### `RemoveMaterialSlotsTests`

- **MR-01** `UnresolvedSlots_AreRemovedAndUndoRestoresThem`.
- **MR-02** `NothingUnresolved_AppliesWithoutAnEntry` (3.2).

## 12. Settings and editor view (task 7)

### `SetScenePlayTests`

- **PY-01** `CameraAndContexts_SetAndRaiseScenePlayChanged`.
- **PY-02** `StartingNodeNotACamera_IsInvalid` [`PlayCameraInvalid`].
- **PY-03** `StartingCameraMissing_IsInvalid` [`PlayCameraInvalid`].
- **PY-04** `RepeatedInputContext_IsInvalid` [`EntryDuplicate`].
- **PY-05** `NullContextEntry_IsInvalid` [`EntryMissing`].
- **PY-06** `ContextNotAnInputAsset_IsInvalid` [`AssetReferenceInvalid`].
- **PY-07** `EquivalentSettings_AppliesWithoutAnEntry` (3.2).

### `SetSceneReferencesTests`

- **RF-01** `References_SetAndRaiseSceneReferencesChanged`.
- **RF-02** `InvalidReference_IsInvalid` → `[DataRow]` `NotAssetScheme`,
  `ScriptOfWrongType`, `ExtraPathNotCanonical`: [`AssetReferenceInvalid`].
- **RF-03** `EqualReferences_AppliesWithoutAnEntry`.

### `SetScriptSlotsTests`

- **SS-01** `Slots_SetAndRaiseScriptSlotsChanged` → `Script`.
- **SS-02** `NodeWithoutScripting_IsInvalid` [`ComponentMissing`].
- **SS-03** `SlotWithoutIdentity_IsInvalid` [`EntryMissing`].
- **SS-04** `TwoSlotsOneIdentity_IsInvalid` [`EntryDuplicate`].
- **SS-05** `ParameterWithoutName_IsInvalid` [`NameEmpty`].
- **SS-06** `AssetParameterNotAnAssetUri_IsInvalid` [`AssetReferenceInvalid`].
- **SS-07** `EqualSlots_AppliesWithoutAnEntry`.

### `SetEditorHiddenTests`

- **EH-01** `Hide_RaisesVisibilityAndKeepsTheDocumentClean` → `B`; undo shows.
- **EH-02** `HideHiddenNode_AppliesWithoutAnEntry` (3.2).
- **EH-03** `HiddenAndShownNodes_RecordOneEntryRaisingOnlyTheChanged` → `B`
  hidden, `Cam` shown, hide both.
- **EH-04** `LockedNode_Hides` (4).
- **EH-05** `MissingIds_AreSkipped` → `B` and a new id: `B` hidden.
- **EH-06** `OnlyMissingIds_AppliesWithoutAnEntry` (4, 3.2).
- **EH-07** `NoIds_IsInvalid` [`NothingToEdit`].
- **EH-08** `WithoutInteractionPort_IsInvalid` [`InteractionPortMissing`].

## 13. Documents

### `SceneContentTests` — functional (3.1, 3.13)

- **DC-01** `Decode_AuthoringRecordsIntoTheContextHistory`.
- **DC-02** `Of_ReturnsTheDocumentsContent`.
- **DC-03** `HasEditsInProgress_FollowsTheOpenGesture`.
- **DC-04** `FinishEdits_CommitsTheOpenGesture`.
- **DC-05** `Reload_RetiresTheOldAuthoring` → old: [`Retired`].
- **DC-06** `Reload_NewAuthoringRecordsIntoTheSameClearedHistory` → no events on
  creation.
- **DC-07** `Reload_CancelsTheOpenGesture`.
- **DC-08** `Close_RetiresTheAuthoring` → [`Retired`]; undo false.
- **DC-09** `EditedScene_SavesAndReopensEqual`.

### `SceneRelocationTests` — functional (3.11)

- **RL-01** `GeometryMoved_RetargetsAndRaisesGeometryChangedWithoutAnEntry` →
  `Mesh`.
- **RL-02** `CubemapMoved_RaisesScenePropertiesChanged`.
- **RL-03** `AfterRetire_ChangesNothing`.

## 14. Scenarios — functional

### `HierarchyScenarioTests`

- **SC-01** `CreatePrimitiveFromToolbar_ThenUndo_RemovesIt` → template, append,
  insert; one entry.
- **SC-02** `DeleteNodesAndFolders_IsOneUndoStep` → `Cam` and `F`.
- **SC-03** `DeleteWithAStaleItem_ChangesNothing`.
- **SC-04**
  `GroupIntoFolderOfAnotherScope_ThenDeleteFolder_KeepsParentAndLocal`.
- **SC-05** `CopyPasteBetweenTwoScenes_UndoInEachIsIndependent`.
- **SC-06** `CanExecuteChecks_ValidateRepeatedly_ChangeNothing` → paste, delete,
  reparent validated ten times; Unchanged.

### `PropertyScenarioTests`

- **SC-07** `ResetAtmosphereLights_AsOneBatch_UndoRestoresBoth`.
- **SC-08** `MoveCamera_PositionAndRotation_IsOneEntry`.
- **SC-09** `GizmoDrag_HundredPreviews_UndoRedo`.
- **SC-10** `InspectorEditThenSave_FinishesTheGestureFirst`.
- **SC-11** `UndoInOneViewDuringDragInAnother_CancelsTheDrag`.
