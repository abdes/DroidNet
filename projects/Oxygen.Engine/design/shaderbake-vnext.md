# ShaderBake vNext Design Specification (Spec-Locked)

Status: `in_progress`
Implementation tracking: this design note plus validated code in `src/Oxygen/Graphics/Direct3D12/Tools/ShaderBake/`
Implementation status note: canonical catalog expansion, deterministic request ordering, duplicate-request-key rejection, build-root layout ownership, manifest snapshot persistence, build-state index persistence with module-scan recovery, action-key calculation, request-local DXC include tracking, dependency fingerprint capture, sharded `.oxsm` artifact read/write, request-scoped compilation, final-archive packing, and request-level dirty analysis for `update` and `rebuild` are implemented. Current `.oxsm` files carry request identity, action key, toolchain hash, primary source hash, resolved include dependency fingerprints, DXIL, and reflection. ShaderBake now also publishes loose DXIL and PDB sidecars beside the final archive under `<out-parent>/dxil/<source-path>/<entry-point>__<request-key>.dxil` and `<out-parent>/pdb/<source-path>/<entry-point>__<request-key>.pdb`, uses external DXC PDB output instead of embedded debug info by default, reuses clean module artifacts, removes stale artifacts after successful runs, emits per-request dev-mode failure logs, and skips final repack when nothing changed. The default runtime output contract now resolves to `bin/Oxygen/<build-config>/<mode>/shaders.bin`. Ninja validation covers selective recompiles, stale-request removal, sidecar-PDB regeneration, `update`/`rebuild` archive parity, clean-cache behavior, working-directory independence, custom `--out`, and preserved `inspect` behavior. Remaining gap: the Visual Studio half of the generator matrix cannot be exercised on this machine because CMake cannot find any installed Visual Studio instance.
Audience: engineers implementing the next ShaderBake iteration and its build integration
Scope: build-time shader compilation, incremental rebuild behavior, intermediate artifact layout, and final OXSL archive emission for Oxygen D3D12 shaders

Cross-references:

- `src/Oxygen/Renderer/Docs/shader-system.md` - runtime shader-system contract
- `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h` - engine shader source of truth
- `src/Oxygen/Graphics/Common/Shaders.h/.cpp` - canonical shader request identity and keying
- `src/Oxygen/Config/PathFinderConfig.h` - final shader library path contract

---

## 1. Objective

Replace the current monolithic ShaderBake execution model with a request-granular, dependency-aware build pipeline that:

- preserves the existing runtime contract of loading one final `shaders.bin` archive,
- stores all intermediary build artifacts under the active build tree,
- emits the final archive to the explicit output path requested by build integration,
- works the same way regardless of whether the outer build uses Ninja, Visual Studio, or another CMake generator.

This specification defines the required behavior of ShaderBake vNext. It is not a brainstorming document.

---

## 2. Non-Negotiable Constraints

1. Runtime never invokes DXC. Runtime continues to load one final OXSL archive only.
2. The engine shader catalog remains the only source of truth for engine-owned compile requests.
3. Intermediary artifacts must live under a caller-provided build root. They must not be written under the source tree.
4. Final artifact path is explicit. It is not derived from the intermediary cache root.
5. ShaderBake owns dependency discovery and incremental validity decisions. Correctness must not depend on Ninja depfiles or any generator-specific feature.
6. Canonical shader request identity must reuse the existing rules in `Oxygen/Graphics/Common/Shaders.cpp`. ShaderBake must not define a second canonicalization model.
7. Dirty detection must not rely on file mtimes alone. Content fingerprints are required for correctness.
8. A failed compile for any catalog request prevents writing the final archive.
9. Final archive writes must be atomic.
10. This design does not add runtime loose-module loading, runtime hot compilation, or a second archive format.

---

## 3. Current Problem

The current pipeline has one coarse build edge for all shader sources and includes, then recompiles the entire engine catalog in one ShaderBake pass before writing `shaders.bin`.

That model has three problems:

1. A change to one shader or include forces full catalog recompilation.
2. ShaderBake does not retain the true include dependency graph for each concrete request.
3. Build integration cannot separate incremental intermediates from the final runtime archive cleanly.

ShaderBake vNext fixes those problems without changing the runtime loader contract.

---

## 4. Terms

### 4.1 Compile Request

One concrete shader request after catalog expansion and canonicalization:

- stage
- canonical source path
- entry point
- sorted define set

### 4.2 Module Artifact

One persisted build output for one compile request. A module artifact contains the compiled DXIL, reflection blob, action identity, and dependency fingerprints for that request.

### 4.3 Build Root

The directory provided to ShaderBake for intermediary state and module artifacts. This directory is generator-local and disposable.

### 4.4 Final Archive

The output OXSL archive written to the explicit path provided to ShaderBake. This is the file the runtime loads through PathFinder resolution.

### 4.5 Action Key

The compile-environment identity for one request. It covers everything that affects compilation output other than source file contents: toolchain version, compiler flags, build mode, include search roots, and artifact schema version. If the action key changes, the request must be recompiled even if no source file changed.

Content-level invalidation (source edits, include edits) is handled separately by dependency fingerprints stored in the module artifact. The action key and the dependency fingerprints together provide full transitive input coverage.

---

## 5. Target Architecture

ShaderBake vNext has four internal stages:

1. Catalog expansion
2. Dirty analysis
3. Request compilation
4. Final archive pack

### 5.1 Catalog Expansion

Input: `kEngineShaders`

Output: a stable, ordered list of canonical compile requests.

Rules:

1. Expand the compile-time catalog into concrete requests exactly as declared.
2. Convert each catalog entry into `ShaderRequest` and canonicalize it using the existing engine logic.
3. Compute the request key using the existing engine `ComputeShaderRequestKey(...)` logic.
4. Preserve deterministic ordering by sorting the expanded list by:
   - source path
   - stage
   - entry point
   - define list

The expanded list is the authoritative module set for the current build.

### 5.2 Dirty Analysis

For each canonical request, ShaderBake loads any existing module artifact from the build root and decides whether the request is dirty.

Dirty analysis uses:

- request key,
- action key,
- source fingerprint,
- include dependency fingerprints,
- artifact integrity checks.

### 5.3 Request Compilation

ShaderBake recompiles dirty requests only.

Each successful compile produces one module artifact under the build root.

### 5.4 Final Archive Pack

If any module artifact changed, if the expanded catalog membership changed, or if the final archive is missing, ShaderBake repacks the final OXSL archive from the current module artifact set.

The final archive contains all catalog requests and no extra modules.

---

## 6. External CLI Contract

ShaderBake vNext exposes the following commands.

### 6.1 `update`

Incremental build entry point.

Behavior:

1. Expand the current catalog.
2. Analyze dirtiness for all requests.
3. Recompile dirty requests.
4. Remove stale artifacts for requests no longer present in the catalog.
5. Repack the final archive only if required.

### 6.2 `rebuild`

Clean rebuild entry point.

Behavior:

1. Ignore all existing module artifacts.
2. Recompile all catalog requests.
3. Repack the final archive.
4. Replace build state with the new successful state.

### 6.3 `inspect`

Existing archive inspection behavior remains and may be extended to inspect module artifacts.

### 6.4 `clean-cache`

Deletes ShaderBake intermediary state under the provided build root only.

It must not delete the final archive unless an explicit `--remove-final-output` flag is added later.

### 6.5 Required Options

All build commands (`update`, `rebuild`, `clean-cache`) accept:

- `--workspace-root <path>`
- `--build-root <path>`
- `--out <path>`

Optional:

- `--shader-root <path>`
- `--oxygen-include-root <path>`
- `--include-dir <path>` (repeatable)
- `--mode dev|production` (default: `dev`)

Resolution rules:

1. Relative paths are resolved against `--workspace-root`.
2. `--build-root` is the root for all ShaderBake intermediary outputs.
3. `--out` is the final archive path. It may be inside or outside the build tree.

---

## 7. Build-Tree Layout Contract

For a given `--build-root`, ShaderBake owns this subtree:

```text
<build-root>/
  state/
    build-state-v1.json
    manifest-v1.json
  modules/
    ab/
      cd/
        <request-key-hex>.oxsm
  logs/
    <request-key-hex>.log
  temp/
```

Rules:

1. `state/build-state-v1.json` is the authoritative incremental state file.
2. `state/manifest-v1.json` is the last successful expanded catalog snapshot.
3. `modules/` stores one module artifact per request key.
4. Module artifacts are sharded into two directory levels using the first four hex characters of the request key: `<hex[0:2]>/<hex[2:4]>/<request-key-hex>.oxsm`.
5. `logs/` is optional in production mode and required in dev mode for failed compiles.
6. `temp/` contains transient files only and may be cleared at process start.

Developer-facing loose shader artifacts are published beside the final archive at:

```text
<out-parent>/
  shaders.bin
  dxil/
    <source-path>/
      <entry-point>__<request-key-hex>.dxil
  pdb/
    <source-path>/
      <entry-point>__<request-key-hex>.pdb
```

These files are intended for PIX, RenderDoc, Nsight, and similar tooling.

---

## 8. Request Identity Contract

ShaderBake must reuse the engine canonicalization and keying rules already used by runtime and PSO caching.

### 8.1 Canonicalization Rules

The canonical request must:

1. normalize source path separators and casing using the existing engine path-normalization logic,
2. validate entry point naming using the existing engine identifier rules,
3. sort defines by name and value using the existing engine ordering,
4. reject duplicate define names.

### 8.2 Request Key

The request key is `ComputeShaderRequestKey(canonical_request)`.

ShaderBake must not invent a second request key algorithm.

### 8.3 Action Key

The action key is a 64-bit hash over:

- canonical request fields (stage, source path, entry point, sorted defines),
- DXC version string,
- fixed DXC argument schema (flags, shader model, HLSL version),
- active build mode affecting DXC arguments (debug vs release optimization/debug-info flags),
- effective include search roots after path resolution (ordered),
- ShaderBake module-artifact schema version.

The action key must change whenever the compile environment would produce different output for the same source bytes.

The action key intentionally excludes source file contents. Content-level invalidation is handled by dependency fingerprints (Section 12, rules 5–7). This separation is required because computing a content-inclusive action key would require preprocessing every request on every invocation, defeating incremental analysis.

---

## 9. Dependency Discovery Contract

ShaderBake must replace the current default DXC include handler with a tracking include handler.

### 9.1 Responsibilities

The tracking include handler must:

1. resolve includes using the same search-root order ShaderBake provides to DXC,
2. canonicalize each resolved dependency path,
3. record each successfully resolved include exactly once per request,
4. preserve dependency order for diagnostics only,
5. return the resolved file contents to DXC,
6. fail deterministically if an include cannot be resolved.

### 9.2 Dependency Set

Each module artifact records:

1. the primary source file fingerprint,
2. one fingerprint per resolved include dependency.

The dependency set must not include unrelated files discovered by directory scanning.

### 9.3 Fingerprint Fields

Each dependency fingerprint stores:

- canonical workspace-relative path,
- content hash (FNV-1a 64-bit over raw file bytes),
- file size in bytes,
- last write time in UTC ticks (platform file-time resolution).

Correctness rule:

- content hash is authoritative,
- size and last write time are allowed only as a fast prefilter before hashing.

---

## 10. Module Artifact Format

Each module artifact is one `.oxsm` binary file.

Encoding conventions:

- All multi-byte integers are little-endian.
- `string16` is a `uint16` byte-length prefix followed by that many UTF-8 bytes (no null terminator). This matches the existing OXSL archive encoding in `ShaderLibraryIO.h`.
- `u64`, `u32`, `u16`, `u8`, `i64` are fixed-width integers.

### 10.1 Header

```text
magic            u32   "OXSM"
version          u32   1
request_key      u64
action_key       u64
toolchain_hash   u64
stage            u8
reserved         7 bytes
source_path      string16 utf-8
entry_point      string16 utf-8
define_count     u16
defines          repeated { name string16, value string16 }
primary_hash     u64
dependency_count u32
dxil_size        u64
reflection_size  u64
```

### 10.2 Dependency Records

For each dependency:

```text
path             string16 utf-8
content_hash     u64
size_bytes       u64
write_time_utc   i64
```

### 10.3 Payload

Payload stores, in order:

1. DXIL bytes
2. reflection bytes

Rules:

1. Defines are stored sorted in canonical order.
2. `primary_hash` is the content hash of the primary source file.
3. `toolchain_hash` keeps compatibility with the final OXSL archive contract and diagnostics.
4. A corrupt or truncated `.oxsm` file is treated as a cache miss.

---

## 11. Build State File Contract

### 11.1 Build State Index

`build-state-v1.json` is UTF-8 JSON with no comments.

Required top-level fields:

```json
{
  "schema_version": 1,
  "shaderbake_version": 1,
  "workspace_root": "...",
  "build_root": "...",
  "module_count": 0,
  "modules": []
}
```

Each `modules[]` entry contains:

- `request_key_hex`
- `action_key_hex`
- `source_path`
- `entry_point`
- `stage`
- `defines`
- `module_artifact_relpath`

Rules:

1. This file is an index only. It must not duplicate the full dependency graph already stored in `.oxsm` unless needed for future performance work.
2. A missing or corrupt build-state file forces a full cache scan of `modules/` or a rebuild, at implementation choice.
3. On successful `update` or `rebuild`, ShaderBake must rewrite this file atomically.

### 11.2 Manifest File

`manifest-v1.json` stores the last successful expanded catalog snapshot and is used to detect catalog membership changes.

Required fields:

```json
{
  "schema_version": 1,
  "request_count": 0,
  "requests": [
    {
      "request_key_hex": "...",
      "source_path": "...",
      "entry_point": "...",
      "stage": "...",
      "defines": []
    }
  ]
}
```

The manifest is compared against the current expanded catalog to detect added, removed, or changed requests.

---

## 12. Dirty Analysis Rules

For `update`, a request is dirty if any of the following is true:

1. no module artifact exists for the request key,
2. module artifact cannot be parsed,
3. stored request fields do not match the current canonical request,
4. stored action key differs from the current action key,
5. primary source file is missing,
6. any dependency file is missing,
7. any dependency content hash changed,
8. an expected loose PDB sidecar beside the final archive is missing for the active build configuration,
9. the request existed previously but last compile did not produce a valid artifact,
10. the request is new relative to the last successful manifest.

For catalog-level state, final repack is required if any of the following is true:

1. any request was recompiled successfully,
2. any stale request was removed,
3. the final archive does not exist,
4. the last successful manifest differs from the current expanded manifest.

Optimization rule:

- ShaderBake may skip content rehashing for a dependency when both stored size and stored write time still match, but it must hash the file again before declaring the dependency unchanged if either field differs.

---

## 13. Compile Flow Contract

For each dirty request, ShaderBake performs the following steps:

1. read primary source bytes,
2. construct compile options from the current fixed DXC schema and request defines,
3. compile through DXC using the tracking include handler,
4. extract reflection,
5. compute dependency fingerprints from the tracking include handler's resolved set,
6. compute the action key from the current compile environment,
7. write `.oxsm.tmp`,
8. atomically rename `.oxsm.tmp` to the final `.oxsm` path.

Dirty requests may be compiled in parallel. The tracking include handler, module artifact writer, and diagnostic emitter must be safe for concurrent use across independent requests.

If compilation fails:

1. no final `.oxsm` is written,
2. existing prior valid `.oxsm` remains untouched,
3. diagnostics are emitted,
4. `update` exits with failure and does not write the final OXSL archive.

This preserves the last known good module cache while preventing publication of a partial final archive.

---

## 14. Final Archive Pack Contract

The final OXSL archive format remains unchanged.

### 14.1 Inputs

Pack consumes exactly one valid module artifact for each current catalog request.

The final archive header must carry the current toolchain hash, computed the same way as the existing `ComputeToolchainHash()` in `Bake.cpp`. This preserves compatibility with the runtime `ShaderLibraryReader` backend-mismatch check.

### 14.2 Output Membership

The final archive contains:

- exactly every current catalog request,
- no requests absent from the current catalog,
- no duplicate request keys.

### 14.3 Write Rules

1. Pack writes to `<out>.tmp` first.
2. After a complete successful write, pack atomically replaces `<out>`.
3. If pack fails, the prior final archive remains untouched.

### 14.4 Pack Ordering

Modules in the final archive are written in the same deterministic order as the expanded catalog ordering from Section 5.1.

### 14.5 Final Path Contract

The final archive path is exactly `--out` after path resolution.

This is the path build integration must supply so that runtime PathFinder resolution locates the same file.

---

## 15. Dev and Production Mode Contract

`--mode` changes policy only. It does not change request identity, dependency rules, artifact formats, or the final runtime contract.

### 15.1 Dev Mode

Required behavior:

1. incremental `update` is the default expected command,
2. failed compile diagnostics are written to `logs/`,
3. debug-capable builds emit loose PDB sidecars beside the final archive and do not use DXC embedded debug info by default,
4. stale module artifacts and stale developer-facing debug exports are removed on successful update.

### 15.2 Production Mode

Required behavior:

1. request identity and dependency rules are identical to dev mode,
2. successful compile logs may be omitted,
3. `rebuild` is allowed and expected for packaging or CI clean-room builds,
4. final archive content must be identical for identical inputs.

Production mode is not a second pipeline.

---

## 16. Build Integration Contract

The outer build system continues to invoke ShaderBake once per target graph evaluation, but ShaderBake itself performs incremental request-level work internally.

### 16.1 Required CMake Invocation Shape

The D3D12 shader CMake should invoke (conceptual; actual quoting and path separators are platform-dependent):

```text
ShaderBake update
  --workspace-root <repo-root>
  --build-root <active-build-dir>/shaderbake
  --out <final-shader-library-path>
```

Optional resolved arguments may also be passed for shader root and include roots.

### 16.2 Build Root Placement

`--build-root` must be inside the active build tree, for example:

- `out/build-ninja/shaderbake`
- `out/build-vs2022/shaderbake`

### 16.3 Final Output Placement

`--out` must be the deploy/runtime path chosen by build integration. For repo-local development this is typically:

- `bin/Oxygen/<build-config>/<mode>/shaders.bin`

If the application or packaging flow chooses a different runtime shader library path, the build must pass that path here.

### 16.4 Generator Independence

ShaderBake correctness must not depend on:

- Ninja depfiles or dyndep,
- Visual Studio incremental build tracking,
- any generator-specific file-change detection,
- process working directory.

All incremental state is owned by ShaderBake under `--build-root`.

---

## 17. Failure Semantics

### 17.1 Update Failure

If any dirty request fails to compile:

1. ShaderBake returns non-zero,
2. no final archive is written,
3. previously valid module artifacts remain available,
4. previously valid final archive remains untouched.

### 17.2 Rebuild Failure

If any request fails during `rebuild`:

1. ShaderBake returns non-zero,
2. the final archive remains untouched,
3. successfully rebuilt module artifacts may remain in the build root for post-failure inspection.

### 17.3 Corrupt Cache State

If build-state or module artifacts are corrupt:

1. ShaderBake logs the issue,
2. treats affected artifacts as cache misses,
3. continues if recovery through recompilation is possible,
4. fails only if required compilation or final pack cannot complete.

---

## 18. Required Implementation Units

The implementation must provide the following responsibilities. File names may vary, but responsibilities must remain separate.

1. catalog expansion from `kEngineShaders`
2. request canonicalization and request-key reuse
3. tracking include handler
4. module artifact reader and writer
5. build-state reader and writer
6. dirty analyzer
7. incremental executor for `update`
8. clean executor for `rebuild`
9. final OXSL packer
10. CLI wiring for `update`, `rebuild`, `inspect`, `clean-cache`

These are required implementation seams, not optional refactoring suggestions.

---

## 19. Acceptance Criteria

ShaderBake vNext is complete only when all of the following are true.

1. Editing one leaf shader source recompiles only the affected concrete requests.
2. Editing one shared include recompiles only requests that resolved that include.
3. Removing a request from the engine shader catalog removes its module artifact from the active manifest and from the final archive.
4. Intermediary state is written only under `--build-root`.
5. Final OXSL archive is written only to `--out`.
6. `update` and `rebuild` produce the same final archive bytes for identical inputs.
7. ShaderBake behavior is identical under at least Ninja and Visual Studio CMake generators.
8. A compile failure never publishes a partial final archive.
9. Runtime continues to load the final archive without any runtime DXC dependency.

---

## 20. Explicit Non-Goals

This specification does not include:

1. runtime shader hot reload,
2. runtime loose-module loading,
3. PSO cache redesign,
4. distributed shader compilation,
5. remote/shared artifact caches,
6. changes to the OXSL runtime format,
7. a generic build system embedded inside ShaderBake.

Those are separate work items.
