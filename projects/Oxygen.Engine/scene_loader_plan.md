# Scene Loader Implementation Plan

**Last updated:** 30 Dec 2025

This file is an **implementation plan only**.
Design, formats, and requirements live in:

- `src/Oxygen/Content/Docs/scenes_and_levels.md`

## Scope

- Implement Scene/Level asset loading for **cooked** scene descriptors.
- Support loading from **loose cooked roots** mounted via `AssetLoader::AddLooseCookedRoot(...)`.
- Support loading from **PAK files** (for end-to-end demo coverage).

## Non-scope

- Cooker/editor authoring pipeline (writer).
- Streaming, world partition, hot reload.
- Renderer feature development (new render paths, new material systems). The demo will **use existing** asset→GPU residency and rendering plumbing.

## Tracking rules

- Every task below is a checkbox.
- When work starts on a task, flip it to `[-]`.
- When finished (code + tests updated), flip it to `[x]`.
- If a task is abandoned, mark it `[ ]` and add a short note in **Change log**.

---

## Milestone M1 — Data module (format + asset type)

### M1.1 Add cooked scene format definitions

- [x] **(D1)** Define the cooked scene v1 structs in `src/Oxygen/Data/PakFormat.h` (single source of truth).
- [x] **(D2)** Add compile-time validation: sizes, alignments, and invariants (pattern-match existing asset descriptors in `PakFormat.h`).
- [x] **(D3)** Ensure no standalone scene format header is exported from `src/Oxygen/Data/CMakeLists.txt`.

### M1.2 Add CPU-side Scene asset type

- [x] **(D4)** Create `src/Oxygen/Data/SceneAsset.h` and `src/Oxygen/Data/SceneAsset.cpp`.
- [x] **(D5)** Define the minimal public API expected by Content and by the demo:
  - access to decoded node data (ids, parent, local transform, name)
  - access to renderable bindings (node index → geometry `AssetKey`)
- [x] **(D6)** Ensure the type participates in the existing typed/object system conventions (match `GeometryAsset` / `MaterialAsset`).
- [x] **(D7)** Wire new files into `src/Oxygen/Data/CMakeLists.txt` (`OXYGEN_DATA_HEADERS` + `OXYGEN_DATA_PRIVATE_SOURCES`).

### M1.3 Data tests (format + basic asset type)

- [x] **(D8)** Add `src/Oxygen/Data/Test/SceneFormat_test.cpp` covering basic invariants (header size, version constants, sentinel values).
- [x] **(D9)** Add `src/Oxygen/Data/Test/SceneAsset_test.cpp` covering basic construction/accessors for `SceneAsset`.
- [x] **(D10)** Register new tests in `src/Oxygen/Data/Test/CMakeLists.txt`.

---

## Milestone M2 — Content module (Scene loader)

### M2.1 Add loader implementation

- [x] **(C1)** Add `src/Oxygen/Content/Loaders/SceneLoader.h`.
- [x] **(C2)** Add `src/Oxygen/Content/Loaders/SceneLoader.cpp` implementing `oxygen::content::loaders::LoadSceneAsset(LoaderContext)`.
- [x] **(C3)** Parse the scene descriptor using `context.desc_reader` and populate a `data::SceneAsset`.
- [x] **(C4)** Record scene → geometry asset dependencies via `context.dependency_collector` (do not call back into `AssetLoader` from decode).

### M2.2 Register the loader in AssetLoader

- [x] **(C5)** Update `src/Oxygen/Content/CMakeLists.txt` to include the new Scene loader files.
- [x] **(C6)** Register `loaders::LoadSceneAsset` in `src/Oxygen/Content/AssetLoader.cpp` alongside existing default loader registration.
- [x] **(C7)** Add `AssetLoader` convenience entrypoints (if needed) consistent with `StartLoadMaterialAsset` / `StartLoadGeometryAsset`:
  - `StartLoadSceneAsset(const data::AssetKey&, ...)`
  - async helper `LoadSceneAssetAsyncImpl(...)` (or reuse `LoadAssetAsync<data::SceneAsset>` directly)

### M2.3 Eager-load geometry (Phase 1.5 behavior)

- [x] **(C8)** After scene decode+publish, request loads for all referenced geometry assets.
- [x] **(C9)** Ensure eager loads do not violate the “decode is pure” contract:
  - decode step only records dependency identities
  - owning-thread publish step schedules eager loads

### M2.4 Content tests

- [x] **(C10)** Add `src/Oxygen/Content/Test/SceneLoader_test.cpp` to validate parsing and dependency recording using `LoaderContext`.
- [ ] **(C11)** Add `src/Oxygen/Content/Test/AssetLoader_scene_loading_test.cpp` to validate end-to-end scene loading from a generated cooked root (loose or PAK), including that scene→geometry dependencies are discovered.
- [x] **(C12)** Register the new tests in `src/Oxygen/Content/Test/CMakeLists.txt` (the `Oxygen.Content.Loaders.Tests` SOURCES list).

---

## Milestone M3 — Tooling (PakGen scene support)

### M3.1 Extend PakGen YAML schema

- [x] **(T1)** Add a `SceneAsset` (or equivalent) model to `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/models.py`.
- [x] **(T2)** Update `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/loader.py` to parse `scenes:` (or `assets: [{type: scene, ...}]`) from YAML into the typed spec model.
- [x] **(T3)** Update `src/Oxygen/Content/Tools/PakGen/src/pakgen/spec/validator.py` to validate:
  - required keys (name/key/source)
  - referenced geometry assets exist
  - node parent indices are in range (for cooked v1 descriptor authoring)
- [x] **(T4)** Add scene asset type dispatch in the packing pipeline so PakGen can emit scene descriptor bytes into the PAK descriptor area.

### M3.2 PakGen tests + sample spec

- [x] **(T5)** Add PakGen unit tests under `src/Oxygen/Content/Tools/PakGen/tests/` covering scene parsing + validation failures.
- [x] **(T6)** Add a minimal `specs/` YAML sample that includes:
  - a couple of geometry assets
  - a scene asset that references them
- [x] **(T7)** Add a golden/snapshot (manifest or plan output) proving scene assets pack deterministically.

### M3.3 PakDump: dump scenes from a PAK

- [ ] **(T8)** Update `src/Oxygen/Content/Tools/PakDump/PakFileDumper.cpp` to recognize the Scene asset type and print a human-readable summary.
- [ ] **(T9)** Decode the scene descriptor bytes using `oxygen::data::SceneFormat`/`SceneAsset` conventions (bounds checks, version checks) and dump:
  - node count + renderable count
  - per-node: id, parent index, local TRS, name (or name index)
  - per-renderable: node index → geometry `AssetKey`
- [ ] **(T10)** Ensure `--hex-dump-assets` still works for scenes and that `--max-data` limits output.

---

## Milestone M4 — Real demo (Examples) loading a PAK and rendering a Scene

### M4.1 Produce a test PAK containing a scene

- [ ] **(E1)** Add an Example-owned PakGen YAML spec (under `Examples/.../Content/` or similar) that includes geometry + materials/textures + a scene.
- [ ] **(E2)** Add build wiring to generate a PAK for the demo (CMake custom command/target invoking PakGen) so the Example can run from a single PAK file.

### M4.2 Example app renders the scene

- [ ] **(E3)** Add a new Example (folder under `Examples/`) that:
  - mounts the generated PAK
  - loads the scene asset
  - instantiates the runtime scene representation
  - renders it via existing renderer + asset residency
- [ ] **(E4)** Ensure scene-driven loads rely on dependency collection (scene→geometry, geometry→materials/textures) rather than bespoke imperative loads.
- [ ] **(E5)** Add minimal runtime logging/assertions so failures are actionable (missing assets, missing dependencies, missing scene nodes).

## Change log

- 30 Dec 2025: Created plan file (no implementation started).
- 30 Dec 2025: Updated scope to include PAK-backed end-to-end rendering demo; added PakGen scene generation milestone; clarified non-scope to exclude new renderer feature work (not GPU residency).
- 30 Dec 2025: Scene format consolidated into `src/Oxygen/Data/PakFormat.h` (no standalone `SceneFormat.h`).
- 30 Dec 2025: Implemented Scene loader + AssetLoader integration and added
  `SceneLoader_test.cpp`. End-to-end `AssetLoader_scene_loading_test.cpp` is
  still pending.
