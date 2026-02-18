# Input Assets In PAK v6

## Purpose

Define a production-ready, data-driven input asset model in `PAK v6` that matches Oxygen's existing action-based runtime semantics exactly, while adding complete tooling coverage (`PakGen`, `PakDump`, `Inspector`).

This document is intentionally constrained to what the engine already supports today in:

- `src/Oxygen/Input/Action.h`
- `src/Oxygen/Input/ActionValue.h`
- `src/Oxygen/Input/ActionState.h`
- `src/Oxygen/Input/ActionTriggers.h`
- `src/Oxygen/Input/InputActionMapping.h`
- `src/Oxygen/Input/InputMappingContext.h`
- `src/Oxygen/Input/InputSystem.h`

No "toy" API is introduced. No new trigger semantics are invented.

## Runtime Ground Truth

The spec must preserve all of these runtime facts:

1. Action value types are exactly:
   - `ActionValueType::kBool`
   - `ActionValueType::kAxis1D`
   - `ActionValueType::kAxis2D`
2. Trigger implementations currently supported by runtime:
   - `Pressed`, `Released`, `Down`, `Hold`, `HoldAndRelease`, `Pulse`, `Tap`, `ActionChain`, `Combo`
3. Trigger behavior model is:
   - `ActionTrigger::Behavior::kExplicit`
   - `ActionTrigger::Behavior::kImplicit`
   - `ActionTrigger::Behavior::kBlocker`
4. Mapping order is semantically relevant inside a context (`InputMappingContext::Update` iterates in insertion order).
5. Context priority is semantically relevant globally (`InputSystem` evaluates active contexts highest priority first).
6. Input consumption is action-driven (`Action::ConsumesInput`) and short-circuits lower-priority contexts in current frame path.
7. Modifiers are not implemented yet (`InputActionMapping` has TODO only). They are out of scope for v6.

## PAK v6 Contract

## AssetType additions

File: `src/Oxygen/Data/AssetType.h`

- `AssetType::kInputAction = 5`
- `AssetType::kInputMappingContext = 6`
- `AssetType::kMaxAssetType = kInputMappingContext`

## ComponentType addition

File: `src/Oxygen/Data/ComponentType.h`

- `ComponentType::kInputContextBinding = 0x54504E49` (`'INPT'`)

## Pak namespace/versioning

File: `src/Oxygen/Data/PakFormat.h`

1. Add `namespace oxygen::data::pak::v6`.
2. `using namespace v5;` in `v6`.
3. `v6::PakHeader` defaults to `version = 6`.
4. `v6::PakFooter` layout remains byte-compatible with `v5::PakFooter`.
5. Default `pak` aliases move from `v5::*` to `v6::*`.

## v6 enums and records

File: `src/Oxygen/Data/PakFormat.h`

- `constexpr uint8_t kInputActionAssetVersion = 1;`
- `constexpr uint8_t kInputMappingContextAssetVersion = 1;`

- `enum class InputActionAssetFlags : uint32_t`
  - `kNone = 0`
  - `kConsumesInput = OXYGEN_FLAG(0)`

- `enum class InputMappingContextFlags : uint32_t`
  - `kNone = 0`

- `enum class InputMappingFlags : uint32_t`
  - `kNone = 0`

- `enum class InputTriggerType : uint8_t`
  - Must mirror `ActionTriggerType` numeric values for implemented triggers:
  - `kPressed, kReleased, kDown, kHold, kHoldAndRelease, kPulse, kTap, kActionChain, kCombo`
  - `kChord` value is reserved in format but rejected by validator/loader until runtime implementation exists.

- `enum class InputTriggerBehavior : uint8_t`
  - `kExplicit, kImplicit, kBlocker`

- `enum class InputContextBindingFlags : uint32_t`
  - `kNone = 0`
  - `kActivateOnLoad = OXYGEN_FLAG(0)`

- `struct InputDataTable` (16 bytes, identical shape to `SceneDataTable`)
  - `uint64_t offset`
  - `uint32_t count`
  - `uint32_t entry_size`

- `struct InputActionAssetDesc` (256 bytes)
  - `AssetHeader header`
  - `uint8_t value_type`
  - `uint8_t reserved0[3]`
  - `uint32_t flags`
  - `uint8_t reserved1[256 - 95 - 1 - 3 - 4]`

- `struct InputMappingContextAssetDesc` (256 bytes)
  - `AssetHeader header`
  - `uint32_t flags`
  - `InputDataTable mappings`
  - `InputDataTable triggers`
  - `InputDataTable trigger_aux`
  - `InputDataTable strings`
  - `uint8_t reserved[256 - 95 - 4 - (16 * 4)]`

- `struct InputActionMappingRecord` (64 bytes)
  - `AssetKey action_asset_key`
  - `uint32_t slot_name_offset`
  - `uint32_t trigger_start_index`
  - `uint32_t trigger_count`
  - `uint32_t flags`
  - `float scale[2]`
  - `float bias[2]`
  - `uint8_t reserved[16]`

- `struct InputTriggerRecord` (96 bytes)
  - `uint8_t type`
  - `uint8_t behavior`
  - `uint16_t reserved0`
  - `uint32_t flags`
  - `float actuation_threshold`
  - `AssetKey linked_action_asset_key`
  - `uint32_t aux_start_index`
  - `uint32_t aux_count`
  - `float fparams[5]`
  - `uint32_t uparams[5]`
  - `uint8_t reserved1[20]`

- `struct InputTriggerAuxRecord` (32 bytes)
  - `AssetKey action_asset_key`
  - `uint32_t completion_states`
  - `uint64_t time_to_complete_ns`
  - `uint32_t flags`

- `struct InputContextBindingRecord` (32 bytes)
  - `uint32_t node_index`
  - `AssetKey context_asset_key`
  - `int32_t priority`
  - `uint32_t flags`
  - `uint32_t reserved`

## Binary semantics mapping

- `InputActionAssetDesc.value_type` maps directly to `ActionValueType` (`0..2` only).
- `InputActionAssetDesc.flags & kConsumesInput` maps to `Action::SetConsumesInput`.
- `InputActionMappingRecord.slot_name_offset` resolves to slot name string; loader resolves via `platform::InputSlots` registry.
- `InputActionMappingRecord.trigger_start_index/trigger_count` define contiguous trigger range for one mapping.
- `InputTriggerRecord.behavior` maps to trigger explicit/implicit/blocker mode.
- `actuation_threshold` applies to all trigger kinds using base threshold semantics.

Parameter binding:

1. `Pressed/Released/Down`:
   - use `actuation_threshold`
2. `Hold`:
   - `fparams[0]=hold_seconds`
   - `uparams[0]=one_shot`
3. `HoldAndRelease`:
   - `fparams[0]=hold_seconds`
4. `Tap`:
   - `fparams[0]=tap_seconds`
5. `Pulse`:
   - `fparams[0]=interval_seconds`
   - `uparams[0]=trigger_on_start`
   - `uparams[1]=trigger_limit`
   - `fparams[1]=jitter_tolerance_seconds`
   - `uparams[2]=phase_align`
   - `fparams[2]=ramp_start_interval_seconds`
   - `fparams[3]=ramp_end_interval_seconds`
   - `fparams[4]=ramp_duration_seconds`
6. `ActionChain`:
   - `linked_action_asset_key`
   - `fparams[0]=max_delay_seconds`
   - `uparams[0]=require_prerequisite_held`
7. `Combo`:
   - `aux_start_index/aux_count` into `InputTriggerAuxRecord[]`
   - aux record `completion_states` maps to `ActionState` bitmask
   - aux record `flags` bit0=`is_breaker` (0 means combo step)

## Scene binding semantics (`INPT`)

- `InputContextBindingRecord.node_index` supports node-attached and scene-global policies:
  - `0` is valid and maps to root/global.
- `context_asset_key` points to `AssetType::kInputMappingContext`.
- `priority` maps directly to `InputSystem::AddMappingContext(..., priority)`.
- `flags & kActivateOnLoad` maps to `InputSystem::ActivateMappingContext`.

## Loader behavior and dependency graph

## SceneLoader

File: `src/Oxygen/Content/Loaders/SceneLoader.h`

1. Validate `INPT` table:
   - `entry_size == sizeof(InputContextBindingRecord)`
   - `node_index < node_count`
   - sorted-by-node invariant consistent with other scene components.
2. Collect dependencies:
   - each `context_asset_key` is an asset dependency.

## Input asset loaders

Files:

- `src/Oxygen/Content/Loaders/InputActionLoader.h`
- `src/Oxygen/Content/Loaders/InputMappingContextLoader.h`

Required symbols:

- `loaders::LoadInputActionAsset`
- `loaders::LoadInputMappingContextAsset`

Rules:

1. `InputMappingContextAsset` must publish transitive dependencies on all referenced action keys:
   - mapping action key
   - chain linked action key
   - combo aux action keys
2. Invalid slot names or invalid trigger parameterization are hard load errors.
3. Unknown trigger type values are hard load errors.
4. `kChord` is explicitly rejected until runtime trigger exists.

## Runtime application in scene service

File: `Examples/DemoShell/Services/SceneLoaderService.cpp`

After scene instantiate:

1. Read `asset.GetComponents<data::pak::InputContextBindingRecord>()`.
2. For each binding:
   - load context asset via `AssetLoader`
   - materialize/lookup runtime `InputMappingContext`
   - call `InputSystem::AddMappingContext(ctx, priority)`
   - if `kActivateOnLoad`, call `InputSystem::ActivateMappingContext(ctx)`

## Tooling updates

## Loose cooked index and table impacts

This design intentionally keeps input data descriptor-local to avoid expanding the loose cooked file graph.

Implications:

1. `container.index.bin` (`LooseCookedIndexFormat v1`) does not require schema changes.
2. `FileKind` in `src/Oxygen/Data/LooseCookedIndexFormat.h` does not require new values for input.
3. No new loose cooked sidecar files are required for input (unlike `scripts.table` / `scripts.data` patterns).
4. Input assets are indexed exactly like existing assets via `AssetEntry`:
   - new `asset_type` values only (`kInputAction`, `kInputMappingContext`).

PAK footer/table implications:

1. No new global resource regions in `PakFooter` are introduced for input.
2. No new global resource tables in `PakFooter` are introduced for input.
3. Existing global tables remain:
   - `texture_table`, `buffer_table`, `audio_table`, `script_resource_table`, `script_slot_table`
4. Input record arrays (`mappings`, `triggers`, `trigger_aux`, `strings`) are internal descriptor payload tables inside `InputMappingContextAsset`, not footer-declared global tables.

Rationale:

- keeps loose cooked tooling stable;
- avoids adding another lifetime model like script resource/slot tables;
- keeps input authoring and dependency tracking asset-centric.

## PakGen (authoring + packing)

Files:

- `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/constants.py`
- `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/models.py`
- `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/validator.py`
- `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/packers.py`
- `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/planner.py`
- `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/writer.py`

Required changes:

1. Add asset types:
   - `"input_action": 5`
   - `"input_mapping_context": 6`
2. Bump `PakPlan.version` default from `5` to `6`.
3. Add spec models and validator rules for:
   - action value type
   - mapping entries
   - trigger behavior/type and parameter compatibility
   - action-key cross references
   - slot name validity
4. Extend planner ordering and deterministic sort with new asset categories.
5. Add packers:
   - `pack_input_action_asset_descriptor`
   - `pack_input_mapping_context_asset_descriptor_and_payload`
   - `pack_input_context_binding_record`
6. Extend scene packer to emit `INPT` component table when `input_context_bindings` exists.

## PakDump (pak file inspection)

Files:

- `src/Oxygen/Content/Tools/PakDump/PakFileDumper.cpp`
- `src/Oxygen/Content/Tools/PakDump/AssetDumpers.h`
- `src/Oxygen/Content/Tools/PakDump/SceneAssetDumper.h`
- new:
  - `src/Oxygen/Content/Tools/PakDump/InputActionAssetDumper.h`
  - `src/Oxygen/Content/Tools/PakDump/InputMappingContextAssetDumper.h`

Required changes:

1. Accept `PAK v6` (`PakFileDumper` currently hard-limits to v4/v5).
2. Register new asset dumpers for `AssetType::kInputAction` and `AssetType::kInputMappingContext`.
3. Scene dumper must include `ComponentType::kInputContextBinding` table summary and record dump.
4. Add output for trigger arrays, aux records, and resolved slot names in mapping context asset dump.

## Inspector (loose cooked inspection)

Files:

- `src/Oxygen/Content/Tools/Inspector/main.cpp`
- `src/Oxygen/Data/LooseCookedIndexFormat.h` (only if new file kinds are introduced)

Required changes:

1. No new `FileKind` is required (input remains descriptor-local).
2. `index` command must display new asset types automatically via updated `AssetType` enum/string conversion.
3. Add dedicated commands for descriptor introspection parity with scripting:
   - `input-actions`
   - `input-contexts`
4. Validation path should verify input descriptors:
   - descriptor size and internal table bounds
   - action key references and trigger param constraints.

## Cross-version compatibility requirements

1. `PakFile` currently rejects v6 (`loads v4/v5 only`); must accept v6.
2. `PakDump` currently rejects v6; must accept v6.
3. Existing v4/v5 behavior must remain unchanged.
4. v6 footer layout remains binary-compatible with v5 footer.

## File-by-file implementation checklist

## Phase 0: tests first

1. `src/Oxygen/Content/Tools/PakGen/tests/test_input_records_generation.py`
2. `src/Oxygen/Content/Tools/PakGen/tests/test_plan_snapshot_input.py`
3. `src/Oxygen/Content/Tools/PakGen/tests/test_pakdump_input_output.py`
4. `src/Oxygen/Content/Test/SceneInputBindingsLoader_test.cpp`
5. `src/Oxygen/Content/Test/AssetLoader_input_deps_test.cpp`
6. `src/Oxygen/Content/Tools/Inspector/Test/InspectorInputDescriptors_test.cpp`

## Phase 1: data format

1. `src/Oxygen/Data/AssetType.h`
   - symbols:
   - `AssetType::kInputAction`
   - `AssetType::kInputMappingContext`
2. `src/Oxygen/Data/ComponentType.h`
   - symbols:
   - `ComponentType::kInputContextBinding`
3. `src/Oxygen/Data/PakFormat.h` (`namespace pak::v6`)
   - symbols:
   - `v6::kInputActionAssetVersion`
   - `v6::kInputMappingContextAssetVersion`
   - `v6::InputActionAssetFlags`
   - `v6::InputTriggerType`
   - `v6::InputTriggerBehavior`
   - `v6::InputActionAssetDesc`
   - `v6::InputMappingContextAssetDesc`
   - `v6::InputActionMappingRecord`
   - `v6::InputTriggerRecord`
   - `v6::InputTriggerAuxRecord`
   - `v6::InputContextBindingRecord`
   - `v6::PakHeader`
   - `v6::PakFooter`
4. `src/Oxygen/Data/ToStringConverters.cpp`
   - symbols:
   - `to_string(AssetType::kInputAction)`
   - `to_string(AssetType::kInputMappingContext)`
   - `to_string(ComponentType::kInputContextBinding)`

## Phase 2: data assets

1. `src/Oxygen/Data/InputActionAsset.h/.cpp`
   - symbols:
   - `class InputActionAsset final : public Asset`
2. `src/Oxygen/Data/InputMappingContextAsset.h/.cpp`
   - symbols:
   - `class InputMappingContextAsset final : public Asset`
3. `src/Oxygen/Data/SceneAsset.h` (`ComponentTraits<pak::InputContextBindingRecord>`)
   - symbols:
   - `ComponentTraits<pak::InputContextBindingRecord>`
4. `src/Oxygen/Data/PakFormatSerioLoaders.h`
   - symbols:
   - `Load(AnyReader&, data::pak::InputActionMappingRecord&)`
   - `Load(AnyReader&, data::pak::InputTriggerRecord&)`
   - `Load(AnyReader&, data::pak::InputTriggerAuxRecord&)`
   - `Load(AnyReader&, data::pak::InputContextBindingRecord&)`
5. `src/Oxygen/Data/CMakeLists.txt`

## Phase 3: content loading

1. `src/Oxygen/Content/Loaders/InputActionLoader.h`
   - symbols:
   - `loaders::LoadInputActionAsset`
2. `src/Oxygen/Content/Loaders/InputMappingContextLoader.h`
   - symbols:
   - `loaders::LoadInputMappingContextAsset`
3. `src/Oxygen/Content/Loaders/SceneLoader.h`
   - symbols:
   - `detail::ValidateComponentTable<data::pak::InputContextBindingRecord>`
   - `ComponentType::kInputContextBinding` branch
4. `src/Oxygen/Content/AssetLoader.h/.cpp`
   - symbols:
   - `RegisterLoader(loaders::LoadInputActionAsset);`
   - `RegisterLoader(loaders::LoadInputMappingContextAsset);`
5. `src/Oxygen/Content/PakFile.h/.cpp`
   - symbols:
   - `PakFile::ReadHeader` accepts `header_.version == 6`

## Phase 4: runtime consumption

1. `Examples/DemoShell/Services/SceneLoaderService.cpp/.h`
   - symbols:
   - `AttachInputContexts(const data::SceneAsset&)`
   - `data::pak::InputContextBindingRecord`

## Phase 5: PakGen

1. `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/constants.py`
2. `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/models.py`
3. `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/validator.py`
4. `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/packers.py`
5. `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/planner.py`
6. `src/Oxygen/Content/Tools/PakGen/src/pakgen/packing/writer.py`
   - symbols:
   - `pack_input_action_asset_descriptor`
   - `pack_input_mapping_context_asset_descriptor_and_payload`
   - `pack_input_context_binding_record`

## Phase 6: PakDump + Inspector

1. `src/Oxygen/Content/Tools/PakDump/PakFileDumper.cpp`
   - symbols:
   - version gate allows `6`
2. `src/Oxygen/Content/Tools/PakDump/AssetDumpers.h`
   - symbols:
   - registry entries for `AssetType::kInputAction`
   - registry entries for `AssetType::kInputMappingContext`
3. `src/Oxygen/Content/Tools/PakDump/SceneAssetDumper.h`
   - symbols:
   - `ComponentType::kInputContextBinding` table output
4. `src/Oxygen/Content/Tools/PakDump/InputActionAssetDumper.h` (new)
   - symbols:
   - `class InputActionAssetDumper final : public AssetDumper`
5. `src/Oxygen/Content/Tools/PakDump/InputMappingContextAssetDumper.h` (new)
   - symbols:
   - `class InputMappingContextAssetDumper final : public AssetDumper`
6. `src/Oxygen/Content/Tools/Inspector/main.cpp`
   - symbols:
   - command handlers `input-actions`, `input-contexts`

## First test cases to add first (exact names)

1. `PakGen_InputActionDescriptor_SizeAndFields`
2. `PakGen_InputMappingContextDescriptor_TablesAndOffsets`
3. `PakGen_InputContextBindingTable_EmittedAsINPT`
4. `PakGen_InputMappingContext_RejectsUnknownSlotName`
5. `PakGen_InputTrigger_RejectsInvalidTypeOrParamShape`
6. `SceneLoader_ValidatesInputContextBindingRecordSize`
7. `SceneLoader_CollectsInputContextAssetDependencies`
8. `AssetLoader_InputMappingContext_PublishesActionDependencies`
9. `PakDump_AcceptsPakV6AndPrintsInputAssets`
10. `PakDump_SceneDumpsINPTComponentTable`
11. `Inspector_IndexShowsInputAssetTypes`
12. `Inspector_InputContextsCommand_DumpsMappingsAndTriggers`

## Non-goals in v6

1. Runtime input modifiers (not implemented yet in `InputActionMapping`).
2. Chord trigger runtime behavior (`kChord` reserved only).
3. Rebinding UI or profile persistence.
4. Multiplayer per-player input profile assets.
