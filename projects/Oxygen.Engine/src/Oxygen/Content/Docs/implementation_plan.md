# Content subsystem implementation plan

**Last updated:** 30 Dec 2025

This file is the **single source of truth for roadmap and status** for the
Content subsystem.

Conceptual model and boundaries live in `overview.md`. If this plan conflicts
with `overview.md`, update the plan.

## Goals (what we are building)

Content provides **CPU-side acquisition** of engine assets/resources, including:

- Decoding assets/resources into CPU objects.
- Enforcing safe lifetimes via caching and dependency tracking.
- Supporting both shipping containers (`.pak`) and editor iteration (loose cooked files).

## Non-goals (by design)

- GPU upload, GPU residency, and GPU memory budgeting.
  Those are Vortex/Graphics-owned (`src/Oxygen/Vortex/Upload/`,
  `src/Oxygen/Vortex/Resources/`, and `src/Oxygen/Graphics/`).

## Implementation tracking (ordered task list)

This table is the **work order**. Higher rows unblock lower rows.
✅ Complete | 🔄 In progress / partial | ❌ Missing | 🧪 Prototype (code exists; not production-ready) | ⏸ Deferred

| # | Status | Priority | Deliverable | Design doc | Notes |
| -: | :---: | :------: | ----------- | ---------- | ----- |
| 1 | ✅ | P0 | Keep `overview.md` authoritative and consistent | `overview.md` | Enforce Content↔Renderer boundary and invariants |
| 2 | ✅ | P0 | Forward-only deps + unified cache + refcount eviction | `deps_and_cache.md` | Implemented in `AssetLoader` + `AnyCache` |
| 3 | ✅ | P0 | Safe unload ordering + tests + docs polish | (in plan) | Release cascade evicts/unloads safely; tests assert resource-before-asset ordering |
| 4 | ✅ | P0 | **Loose cooked content** (filesystem-backed) | `loose_cooked_content.md` | End-to-end mount + descriptor discovery + table/data readers + focused tests + diagnostics are in place. |
| 5 | ✅ | P0 | Loose cooked **index** (AssetKey→descriptor path, resources) | `loose_cooked_content.md` | `container.index.bin` v1 schema + parser + strict mount-time validation are complete; editor-facing virtual-path resolution is available via `VirtualPathResolver`. |
| 6 | ❌ | P0 | **Scene/Level asset** (editor maps/levels) | `scenes_and_levels.md` | Biggest current hole; defines composition and references |
| 7 | ❌ | P0 | Minimal scene serialization toolchain (cooked, loose) | `scenes_and_levels.md` | Separate from runtime loader; produces cooked scene format |
| 8 | ❌ | P0 | Asset database (project index, GUID ownership, metadata) | `asset_database_and_ddc.md` | Editor-oriented; maps GUIDs to source/cooked artifacts |
| 9 | ❌ | P0 | Derived data cache (DDC) for cooked artifacts | `asset_database_and_ddc.md` | Keyed by (inputs + import settings + platform) |
| 10 | ✅ | P1 | Async AssetLoader (CPU acquisition) + in-flight dedup | `truly-async-asset-loader.md` | Coroutine-based load APIs are implemented and the owning-thread publish model is enforced; in-flight dedup is in place |
| 11 | ❌ | P1 | Cancellation propagation + “no partial insertion” guarantee | (TBD) | The loader supports global cancellation via the nursery, but does not yet expose per-operation cancellation tokens or a documented rollback guarantee |
| 12 | ❌ | P1 | Content observability counters + scoped tracing hooks | (short design below) | Timing: IO/decode/cache-hit/miss/evictions |
| 13 | ❌ | P1 | Hot reload for loose cooked content | `hot_reload.md` | File watch → invalidation → reload dependents |
| 14 | ❌ | P1 | Hot reload safety model (generation IDs, handle policy) | `hot_reload.md` | Defines what remains stable for the editor |
| 15 | ❌ | P2 | Chunk metadata + partial decode hooks (CPU-side) | (TBD) | Complements `chunking.md` (format-level) |
| 16 | ❌ | P2 | Dependency analyzer output (JSON) | `tooling_and_diagnostics.md` | Graph extraction + refcounts + fan-out stats |
| 17 | ❌ | P2 | Perf benchmark suite for Content | `tooling_and_diagnostics.md` | Cold/warm/parallel burst scenarios |
| 18 | ❌ | P3 | Memory mapping prototype for PAK and/or loose cooked | (TBD) | Optional optimization; not required for editor unblock |
| 19 | ⏸ | P4 | Stable resource indices across regenerated loose cooked roots | `loose_cooked_content.md` | Future enhancement: keep `ResourceIndexT` values stable across recooks (patchability / determinism); runtime correctness only requires per-root internal consistency |

**Policy:** Any “big feature” above must have its own design doc (linked).
Small enhancements should be specified in short design notes inside this plan.

## Current state (what Content already does well)

- `.pak` container reading via `PakFile` (directory, resource tables, data regions).
- Loader model (`LoaderContext`) with separate descriptor and data-region readers.
- Unified caching (`AnyCache` + refcount eviction) for assets and resources.
- Forward-only dependency registration (asset→asset, asset→resource) with debug cycle rejection.

Corrections to older wording:

- `data::AssetKey` is a **128-bit GUID** (no additional metadata in the type itself).

---

## Roadmap phases

Phases communicate dependency order, not strict calendar time.

### Phase 1 (foundation) — ✅ complete

- Content sources seam + deterministic mounting
- Async coroutine-based load APIs (assets/resources) with owning-thread publish
- Unified cache + dependencies + release cascades

### Phase 1.5 (editor unblock: loose cooked + scenes) — 🔄 in progress

Primary objective: **render real content in the editor without PAK packing**.

Deliverables:

- Refactor runtime loader around a "content source" seam (PAK now; loose cooked later).
- Register a filesystem-backed content container (“loose cooked root”). ✅
- Resolve `AssetKey → descriptor file` via a cooked index. ✅
- Load assets/resources through the existing `LoaderContext` pipeline.
- Define and load a minimal **Scene/Level asset** that instantiates Geometry/Material.

Acceptance criteria:

- Editor can load a mesh + material + textures from loose cooked outputs.
- Editor can load a simple scene/level that references those assets.
- Dependency tracking and release semantics remain identical to PAK mode.

### Phase 2 (cancellation + completeness) — ❌ planned

Objective: improve ergonomics and robustness on top of the completed async
AssetLoader (still ending at **DecodedCPUReady**).

Deliverables:

- Add per-operation cancellation tokens and a documented rollback guarantee.
- Expand async coverage as new asset families are added (for example, Scene/Level).
- Keep in-flight dedup behavior consistent across newly added types.

### Phase 3 (hot reload + diagnostics) — ❌ planned

Objective: iterate quickly in editor and inspect Content behavior.

- Hot reload for loose cooked mode (file watch + invalidation + dependent rebuild).
- Diagnostics counters, structured events, and graph export.

### Phase 4 (streaming/partial decode) — ❌ planned

Objective: support large scenes and streaming without entangling GPU residency.

- Chunk metadata schema + partial decode hooks.
- Prefetch/priority hints (CPU-side only).

## Small feature designs (inline)

These are intentionally short; anything that grows beyond a page becomes its own doc.

### Content observability counters (P1)

Goal: make Content measurable without requiring a profiler.

Proposed minimal API surface:

- Counters: cache hit/miss, bytes read, decode time, eviction count, dependency edges registered.
- Scope timing: RAII helper around load phases (find-entry, descriptor read, data read, decode).
- Sink: either engine logging, a pull-based snapshot struct, or a callback hook.

Constraints:

- Must not allocate per operation in the hot path.
- Must not impose global locking across loads (especially once async lands).

### Debugging failing Content tests (pakgen + PakDump)

Some Content unit tests generate a `.pak` on the fly from a YAML spec.

Facts and locations:

- YAML specs live in `src/Oxygen/Content/Test/TestData/*.yaml`.
- PAK generation happens in `AssetLoaderLoadingTest::GeneratePakFile(...)`.
  - Primary invocation: `pakgen build <spec.yaml> <output.pak> --deterministic`
  - Fallback invocation: `python -m pakgen.cli build <spec.yaml> <output.pak> --deterministic`
- Generated PAKs are written under the system temp directory (see
  `std::filesystem::temp_directory_path()`), in a folder named
  `oxygen_asset_loader_tests`.

Repro steps (when a test fails):

1. Identify the YAML spec name used by the test (e.g. `material_with_textures`).
2. Re-run the same `pakgen build` command manually to reproduce deterministically.
3. Run PakDump against the generated `.pak` to inspect directory entries,
   resource tables, and optionally asset/resource hex dumps.

PakDump notes:

- Build target name: `Oxygen.Cooker.PakDump`.
- Example: `Oxygen.Cooker.PakDump <path-to.pak> --verbose --show-data`.

## Detailed feature matrix (status snapshot)

This matrix is a convenience view. The ordered task list above is authoritative.

### Containers / sources

| Feature | Status | Notes |
| ------- | ------ | ----- |
| PAK file container (`PakFile`) | ✅ | Stable |
| Loose cooked container | ✅ | Complete (mount, index validation, descriptor + table/data readers, virtual-path resolver). Future enhancement tracked under #19 (stable indices across recooks). |
| Container registration + ordering | ✅ | `AssetLoader` registers sources deterministically; PAK and loose cooked sources are both functional |

### Loading + lifecycle

| Feature | Status | Notes |
| ------- | ------ | ----- |
| Synchronous load | ✅ | Current `AssetLoader::LoadAsset/LoadResource` |
| Dependency registration | ✅ | Forward-only maps + cache Touch |
| Safe unloading | ✅ | Resource-before-asset unload ordering asserted in tests |
| Async AssetLoader (CPU acquisition) | ✅ | Coroutine-based load APIs exist (`LoadAssetAsync` and `LoadResourceAsync`) with owning-thread publish and in-flight dedup |
| Per-operation cancellation + rollback | ❌ | No cancellation token API or “no partial insertion” guarantee yet |
| Hot reload | ❌ | Requires invalidation + dependent rebuild |

### Asset families

| Feature | Status | Notes |
| ------- | ------ | ----- |
| Geometry + Material + Texture/Buffer | ✅ | Cooked formats supported |
| Scene/Level | ❌ | Immediate editor hole |
| Animation | ❌ | Future |
| Audio | ❌ | Future |

## Glossary

| Term | Definition |
| ---- | ---------- |
| Asset | A keyed, first-class descriptor (Geometry, Material, Scene/Level, …) |
| Resource | Typed bulk data referenced by an index in a container’s resource tables |
| DecodedCPUReady | Content terminal state: decoded CPU objects cached and safe to use |
| Loose cooked content | Cooked Oxygen formats stored on disk without PAK packaging |
| UploadCoordinator (Renderer) | Renderer-owned staging→GPU submission + fence tracking |

## Cross-reference map

| Topic | File |
| ----- | ---- |
| Conceptual model + boundaries | `overview.md` |
| Dependency + cache mechanics | `deps_and_cache.md` |
| PAK layout & alignment | `chunking.md` |
| Loose cooked content design | `loose_cooked_content.md` |
| Scene/Level (maps) design | `scenes_and_levels.md` |
| Asset DB + DDC design | `asset_database_and_ddc.md` |
| Async loader architecture | `truly-async-asset-loader.md` |
| Hot reload design | `hot_reload.md` |
| Streaming + chunks (runtime-facing) | (TBD) |
| Tooling + diagnostics | `tooling_and_diagnostics.md` |
| GPU uploads (Renderer) | `../../Renderer/Upload/README.md` |

## Update policy

- Status/roadmap changes must land here first.
- Big features require a dedicated design doc linked above.
- Other documents must not carry their own competing status tables.
