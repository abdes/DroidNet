# Vortex Renderer Implementation Status

Status: `in_progress (pre-execution baseline; scaffold exists but build integration is incomplete)`

This document is the resumability ledger for the Vortex renderer effort. It is
the place to record what is actually in the repo, what has been verified, what
is still missing, and exactly where implementation should resume next.

Related:

- [PRD.md](./PRD.md)
- [ARCHITECTURE.md](./ARCHITECTURE.md)
- [DESIGN.md](./DESIGN.md)
- [PROJECT-LAYOUT.md](./PROJECT-LAYOUT.md)
- [PLAN.md](./PLAN.md)

Historical references:

- [parity-analysis.md](./parity-analysis.md) — rewrite-direction analysis that
  informed the LLD package
- [vortex-initial-design.md](./vortex-initial-design.md) — predecessor
  migration sketch; no longer the active low-level design baseline

## Current Verified Baseline

The repo currently contains a partial Vortex scaffold, not a validated
implementation:

- `src/Oxygen/Vortex/CMakeLists.txt` declares the `Oxygen.Vortex` module, but
  its file lists currently contain only `api_export.h`.
- `src/Oxygen/Vortex/api_export.h` exists.
- `src/Oxygen/Vortex/Test/CMakeLists.txt` exists, but the link test block is
  still commented out.
- `src/Oxygen/Vortex/` already contains the planned top-level directory tree,
  but the directories are placeholders only (`.gitkeep` files).
- `src/Oxygen/CMakeLists.txt` does **not** add `Vortex`, so the module is not
  part of the main build graph yet.
- The active production renderer remains `Oxygen.Renderer`; the current desktop
  path is still driven by `Renderer` plus `ForwardPipeline`.
- The three facade patterns Vortex must preserve already exist in the legacy
  renderer: `ForSinglePassHarness()`, `ForRenderGraphHarness()`, and
  `ForOffscreenScene()`.

## Validation Evidence

Validated in this session:

- repo inspection:
  - `src/Oxygen/Vortex/CMakeLists.txt`
  - `src/Oxygen/Vortex/api_export.h`
  - `src/Oxygen/Vortex/Test/CMakeLists.txt`
  - `src/Oxygen/CMakeLists.txt`
  - `src/Oxygen/Renderer/Renderer.h`
  - `src/Oxygen/Renderer/Pipeline/ForwardPipeline.h`
- build-system check:
  - `cmake --build --preset windows-debug --target Oxygen.Vortex --parallel 4`
  - Result: FAIL with `unknown target 'Oxygen.Vortex'`
  - Meaning: the Vortex module scaffold exists on disk, but it is not wired
    into the parent Oxygen CMake graph yet

Not validated in this session:

- no successful Vortex target build
- no Vortex link test
- no Vortex unit tests
- no runtime smoke

## Current Status by Slice

### V.0 Scaffold

Status: `in_progress`

Implemented evidence:

- `src/Oxygen/Vortex/` directory tree exists
- `src/Oxygen/Vortex/CMakeLists.txt` exists
- `src/Oxygen/Vortex/api_export.h` exists
- `src/Oxygen/Vortex/Test/CMakeLists.txt` exists

Remaining to clear exit gate:

- add `add_subdirectory("Vortex")` to `src/Oxygen/CMakeLists.txt`
- confirm the target name/alias emitted by the module matches the intended
  `oxygen::vortex` linkage surface
- run a successful build of the Vortex module through the normal preset

Resume point:

- wire the module into the parent CMake graph first; do not start copying
  implementation slices while the scaffold target is still absent from the
  build

### V.1 Cross-Cutting Types

Status: `not started`

Required baseline:

- copy the pipeline-agnostic type headers from legacy `Renderer/Types/`
- rename namespace/include/export-macro surfaces to `oxygen::vortex` and
  `OXGN_VRTX_*`
- add the headers to `OXYGEN_VORTEX_HEADERS`

Exit gate:

- full build passes with the copied type headers in the Vortex target

### V.2 Upload, Resources, ScenePrep

Status: `not started`

Required baseline:

- copy `Upload/`, `Resources/`, and `ScenePrep/` into `src/Oxygen/Vortex/`
- keep this slice mechanical only

Exit gate:

- full build passes

### V.3 Internal Utilities

Status: `not started`

Required baseline:

- copy the renderer-core internal utilities called out in
  `vortex-initial-design.md`

Exit gate:

- full build passes

### V.4 Pass Base Classes

Status: `not started`

Required baseline:

- copy the base pass classes into `src/Oxygen/Vortex/Passes/`
- adapt `RenderPass` to the Vortex `RenderContext`

Exit gate:

- full build passes

### V.5 Pipeline Framework

Status: `not started`

Required baseline:

- copy the pipeline framework into `src/Oxygen/Vortex/Pipeline/`
- update pipeline types to point at Vortex-owned types instead of legacy ones

Exit gate:

- full build passes

### V.6 Renderer Orchestrator

Status: `not started`

Required baseline:

- copy the renderer shell and direct dependencies into `src/Oxygen/Vortex/`
- strip domain-specific environment, shadow, lighting, diagnostics, and VSM
  ownership from the initial Vortex renderer
- preserve the frame loop skeleton, view registry, composition queue, upload
  services, capability queries, and the three non-runtime facades

Exit gate:

- full build passes
- Vortex renderer instantiates with an empty capability set
- frame loop executes as an empty shell without domain systems

### V.7 Smoke Test

Status: `not started`

Required baseline:

- add `src/Oxygen/Vortex/Test/Link_test.cpp`
- enable the commented link-test block in `src/Oxygen/Vortex/Test/CMakeLists.txt`

Exit gate:

- Vortex link test passes
- full build passes

## Architectural Resume Notes

When implementation resumes, keep these baseline facts explicit:

- the active Vortex source-of-truth package is:
  `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md`, `PROJECT-LAYOUT.md`, and `PLAN.md`
- Vortex is a replacement desktop renderer module, not an incremental rename of
  the current forward-first renderer.
- The current legacy renderer is still the live implementation and the current
  source of reusable substrate.
- The first production-shaped compatibility surface to preserve is the trio of
  non-runtime facades.
- The current desktop runtime architecture is still `ForwardPipeline`-driven,
  so any claim that Vortex is active must be backed by concrete code and build
  evidence, not by design docs alone.

## Update Rules For This File

Each implementation session must update this document with evidence, not
intention.

Required on every meaningful change:

- changed files
- commands run
- result of each validation command
- exact remaining blocker to the next exit gate

Mandatory discipline:

- do not mark a slice complete unless code exists, related docs are updated,
  and validation evidence is recorded here
- if build or tests were not run, keep the slice `in_progress` and list the
  missing validation delta explicitly
- if scope changes or the current design is found incomplete, update the vortex
  design docs before claiming further implementation progress

## Recommended Next Step

1. Keep future Vortex implementation and status updates aligned to the current
   five-document LLD package:
   `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md`, `PROJECT-LAYOUT.md`, and
   `PLAN.md`.
2. Finish V.0 by wiring `src/Oxygen/Vortex` into `src/Oxygen/CMakeLists.txt`
   and proving the target builds.
3. Only then begin the mechanical copy slices V.1 through V.5.
