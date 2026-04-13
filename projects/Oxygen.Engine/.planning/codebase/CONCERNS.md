# Codebase Concerns

**Analysis Date:** 2026-04-03

## Tech Debt

**Build bootstrap drift:**
- Issue: `tools/generate-builds.ps1` calls the missing Ninja preset name `windows-ninja` and hardcodes the generator `Visual Studio 18 2026`, while `cmake --list-presets` exposes `windows-default` and `windows-asan` and `README.md` still documents a Visual Studio 2022-based setup.
- Files: `tools/generate-builds.ps1`, `tools/presets/WindowsPresets.json`, `tools/presets/BasePresets.json`, `CMakePresets.json`, `README.md`
- Impact: new environments can fail before a build tree exists, and the bootstrap path can drift independently from the checked-in presets.
- Fix approach: source preset names from `tools/presets/WindowsPresets.json`, derive the Visual Studio generator from installed tooling or Conan metadata, and add a smoke test that exercises the script in CI.

**Runtime frame phases are stubbed behind production entry points:**
- Issue: `AsyncEngine` exposes a full deterministic phase model, but key stages are TODOs or no-ops: context setup, epoch advance, network reconciliation, deterministic RNG setup, adaptive budget management, engine-owned post-parallel work, and crash dump detection.
- Files: `src/Oxygen/Engine/AsyncEngine.cpp`, `src/Oxygen/Engine/README.md`, `src/Oxygen/Engine/Docs/phase-summary.md`
- Impact: the engine advertises a richer runtime contract than the executable implementation delivers, especially for multiplayer, determinism, runtime budgeting, and diagnostics.
- Fix approach: either hide incomplete phases behind explicit feature gates or implement them end-to-end and add integration coverage for each phase boundary.

**Public API leaks internal headers:**
- Issue: module CMake files still export internal or detail-level headers and carry TODOs to stop doing so.
- Files: `src/Oxygen/Scene/CMakeLists.txt`, `src/Oxygen/Physics/CMakeLists.txt`, `src/Oxygen/PhysicsModule/CMakeLists.txt`
- Impact: downstream code can couple to unstable implementation details, turning internal refactors into breaking API changes.
- Fix approach: move detail headers into private file sets, keep only stable façades public, and add source-contract tests for the supported include surface.

**Large first-party orchestration files concentrate change risk:**
- Issue: several first-party files have grown into multi-thousand-line units that mix orchestration, policy, and backend behavior.
- Files: `src/Oxygen/Content/AssetLoader.cpp` (~4090 lines), `src/Oxygen/Renderer/Renderer.cpp` (~3588 lines), `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp` (~3506 lines), `src/Oxygen/Cooker/Pak/PakPlanBuilder.cpp` (~2997 lines), `Examples/DemoShell/Services/SceneLoaderService.cpp` (~3038 lines), `Examples/DemoShell/Services/EnvironmentSettingsService.cpp` (~2924 lines)
- Impact: review scope, merge conflicts, compile times, and regression blast radius all grow around a handful of files.
- Fix approach: split by pipeline stage or service responsibility and preserve behavior with smaller, focused tests around the new seams.

**Repo-local ignore coverage is minimal:**
- Issue: `.gitignore` only lists `ConanPresets-*.json`, `.omx`, and `conductor`, while the working tree contains generated or machine-local paths such as `out/`, `.vs/`, `.pytest_cache/`, `logs/`, `packages/`, `imgui.ini`, and `flicker*.log` outputs.
- Files: `.gitignore`, `out/`, `.vs/`, `.pytest_cache/`, `logs/`, `packages/`, `imgui.ini`, `flicker.log`, `flicker-slow.log`, `flicker-rgb-cubes.log`, `flicker-rgb-cubes-v2.log`
- Impact: contributors must rely on global ignores or manual discipline to avoid noisy worktrees and accidental artifact commits.
- Fix approach: expand repo-local ignore rules and keep generated outputs in dedicated ignored roots.

## Known Bugs

**Graphics backend invalidation aborts frame execution:**
- Symptoms: the engine throws `std::logic_error("Graphics backend no longer valid.")` instead of degrading or shutting down cleanly.
- Files: `src/Oxygen/Engine/AsyncEngine.cpp`
- Trigger: the weak graphics backend expires during frame start, post-simulation, or frame finalization.
- Workaround: keep the graphics backend alive for the full engine lifetime and avoid backend invalidation during frame execution.

**Surface removal leaves cleanup incomplete:**
- Symptoms: `FrameContext::RemoveSurfaceAt` erases the surface and flag entries but leaves a FIXME for view cleanup tied to the removed pointer.
- Files: `src/Oxygen/Core/FrameContext.cpp`, `src/Oxygen/Core/FrameContext.h`
- Trigger: removing a registered surface after view/presentable state has already been established.
- Workaround: prefer bulk clear/recreate flows or audit dependent view state manually after removal.

## Security Considerations

**Tool provisioning downloads are not hash-pinned:**
- Risk: setup scripts fetch SDK archives or headers from live endpoints without a repo-pinned checksum or signature verification step.
- Files: `GetDXC.ps1`, `GetAftermath.ps1`, `GetPIX.ps1`, `GetRenderDoc.ps1`, `README.md`
- Current mitigation: basic archive extraction checks and a simple symbol-string validation in `GetRenderDoc.ps1`.
- Recommendations: pin approved versions in repo metadata, verify hashes or signatures before install, and cache vetted artifacts for reproducible bootstrap.

**Broad public include surface increases exposure to unstable internals:**
- Risk: exported detail headers make it easy for application code to depend on internal implementation structures that were not designed as hardened public APIs.
- Files: `src/Oxygen/Scene/CMakeLists.txt`, `src/Oxygen/Physics/CMakeLists.txt`
- Current mitigation: TODO comments only.
- Recommendations: reduce the public header set and enforce the supported include boundary with compile-only API tests.

## Performance Bottlenecks

**Compilation and review hotspots sit in a few very large translation units:**
- Problem: large, central files dominate rebuild cost and change risk.
- Files: `src/Oxygen/Content/AssetLoader.cpp`, `src/Oxygen/Renderer/Renderer.cpp`, `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`, `src/Oxygen/Cooker/Pak/PakPlanBuilder.cpp`
- Cause: orchestration, policy, and backend details are implemented together instead of behind smaller internal seams.
- Improvement path: split orchestration from backend adapters and data-policy code, then keep targeted regression tests close to each slice.

**Scene sizing relies on hard-coded capacities:**
- Problem: scene and component sizing is driven by fixed magic numbers instead of a shared capacity policy.
- Files: `src/Oxygen/Scene/Detail/TransformComponent.h`, `Examples/RenderScene/MainModule.cpp`
- Cause: `kExpectedPoolSize = 10000` and `kSceneInitialCapacity = 10000` are both marked as hacks/FIXMEs.
- Improvement path: centralize capacity configuration, derive defaults from scene graph or benchmark data, and run scene-size sweeps before changing allocator behavior.

## Fragile Areas

**Async engine phase orchestration:**
- Files: `src/Oxygen/Engine/AsyncEngine.cpp`, `src/Oxygen/Engine/Test/AsyncEngine_api_test.cpp`, `src/Oxygen/Engine/Test/ModuleManager_test.cpp`, `src/Oxygen/Engine/Test/FrameContext_test.cpp`
- Why fragile: deterministic phase order, weak-pointer ownership, runtime budgeting, and placeholder features all live in one file; several branches terminate with exceptions instead of recovery.
- Safe modification: change one phase at a time, keep sequencing assertions explicit, and run engine/module-manager/frame-context tests after every edit.
- Test coverage: compile-time and module-manager tests exist, but no repository test exercises the TODO phases or the graphics-invalidation failure paths.

**Surface/view lifecycle in frame context:**
- Files: `src/Oxygen/Core/FrameContext.h`, `src/Oxygen/Core/FrameContext.cpp`, `src/Oxygen/Engine/Test/FrameContext_test.cpp`
- Why fragile: surfaces, views, presentable flags, and future shared game data are coupled inside one mutable context object.
- Safe modification: preserve index and lifetime invariants, add regression tests before changing removal semantics, and avoid mixing unrelated data-model changes with surface lifecycle edits.
- Test coverage: `FrameContext_test.cpp` exists, but the removal cleanup FIXME and empty `GameData` placeholder leave real gaps.

**Physics backend bridge:**
- Files: `src/Oxygen/Physics/Jolt/JoltSoftBodies.cpp`, `src/Oxygen/Physics/Jolt/JoltArticulations.cpp`, `src/Oxygen/Physics/SoftBody/SoftBodyDesc.h`, `src/Oxygen/Physics/Test/Jolt/SoftBody/Jolt_softbody_domain_test.cpp`, `src/Oxygen/Physics/Test/Jolt/Articulation/Jolt_articulation_domain_test.cpp`
- Why fragile: the public API exposes features that the backend deliberately rejects or defers, so the contract is wider than the implementation.
- Safe modification: update API docs, backend code, and domain tests together so the accepted behavior stays explicit.
- Test coverage: tests intentionally lock in `PhysicsError::kNotImplemented`, which protects behavior but also normalizes missing capability.

**Build/install tooling:**
- Files: `tools/generate-builds.ps1`, `tools/cli/oxybuild.ps1`, `tools/cli/oxyrun.ps1`, `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt`
- Why fragile: bootstrap depends on external Conan/CMake state and several scripts staying in sync; `pakgen_install` is still a placeholder target.
- Safe modification: keep preset names and generator names in one source of truth and add smoke tests for the PowerShell entry points.
- Test coverage: no repository tests exercise these PowerShell flows or the `pakgen_install` path.

## Scaling Limits

**Scene/component pool sizing:**
- Current capacity: `TransformComponent` and the `RenderScene` example both default to about `10000` scene entries.
- Limit: larger scenes either over-allocate memory up front or drift away from assumptions baked into scene/component pool sizing.
- Scaling path: move capacities into shared configuration or allocator heuristics and benchmark against small, medium, and very large scenes.
- Files: `src/Oxygen/Scene/Detail/TransformComponent.h`, `Examples/RenderScene/MainModule.cpp`

**Single-file orchestration hotspots:**
- Current capacity: a small set of files owns a large percentage of runtime orchestration logic.
- Limit: parallel development and incremental builds degrade when unrelated work collides in `AssetLoader.cpp`, `Renderer.cpp`, or `AsyncEngine.cpp`.
- Scaling path: separate service boundaries and pipeline stages so new work lands in smaller units with isolated test surfaces.
- Files: `src/Oxygen/Content/AssetLoader.cpp`, `src/Oxygen/Renderer/Renderer.cpp`, `src/Oxygen/Engine/AsyncEngine.cpp`

## Dependencies at Risk

**Floating external tool provisioning:**
- Risk: setup scripts resolve live SDK artifacts at install time, so bootstrap depends on third-party availability and whatever version those endpoints serve.
- Impact: reproducibility and supportability degrade across developers and CI agents.
- Migration plan: keep a version manifest in-repo, pin URLs plus hashes, and prefer vetted cached artifacts for DXC, PIX, Aftermath, and RenderDoc.
- Files: `GetDXC.ps1`, `GetAftermath.ps1`, `GetPIX.ps1`, `GetRenderDoc.ps1`, `README.md`

**Experimental Conan multi-config path:**
- Risk: build generation depends on the experimental setting `tools.cmake.cmakedeps:new=will_break_next`.
- Impact: Conan upgrades can break the bootstrap path even when project code is unchanged.
- Migration plan: isolate the experimental flag behind a repo option, document the Conan version contract, and add a compatibility check to `tools/generate-builds.ps1`.
- Files: `tools/generate-builds.ps1`, `conanfile.py`

## Missing Critical Features

**Input action modifiers and transformers:**
- Problem: input mappings omit ignore flags, mapping modifiers, and initial-value transformers.
- Blocks: richer rebinding flows, modifier-aware actions, and parity with the documented input design.
- Files: `src/Oxygen/Input/InputActionMapping.h`, `src/Oxygen/Input/InputActionMapping.cpp`, `design/content-pipeline/pak-v7-input-format-historical-reference.md`

**Network reconciliation, deterministic RNG, and adaptive budgeting:**
- Problem: core engine phases are declared in the frame model but left as TODO/no-op logic.
- Blocks: authoritative multiplayer, reproducible simulation randomness, and frame-time budget enforcement.
- Files: `src/Oxygen/Engine/AsyncEngine.cpp`, `src/Oxygen/Engine/README.md`, `src/Oxygen/Engine/Docs/phase-summary.md`

**Soft-body anchoring and articulation materialization:**
- Problem: anchored soft bodies return `PhysicsError::kNotImplemented`, and articulation flush only returns pending change counts instead of building solver graphs.
- Blocks: full soft-body and articulation feature parity on the Jolt backend.
- Files: `src/Oxygen/Physics/Jolt/JoltSoftBodies.cpp`, `src/Oxygen/Physics/Jolt/JoltArticulations.cpp`, `src/Oxygen/Physics/SoftBody/SoftBodyDesc.h`, `src/Oxygen/Physics/Test/Jolt/SoftBody/Jolt_softbody_domain_test.cpp`

**PakGen deployment install:**
- Problem: `pakgen_install` is a placeholder target that only depends on the editable install path.
- Blocks: a reliable packaged install flow for `PakGen` outside a development checkout.
- Files: `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt`

## Test Coverage Gaps

**Bootstrap and CLI scripts:**
- What's not tested: `tools/generate-builds.ps1`, `tools/cli/oxybuild.ps1`, `tools/cli/oxyrun.ps1`, and the SDK downloader scripts.
- Files: `tools/generate-builds.ps1`, `tools/cli/oxybuild.ps1`, `tools/cli/oxyrun.ps1`, `GetDXC.ps1`, `GetAftermath.ps1`, `GetPIX.ps1`, `GetRenderDoc.ps1`
- Risk: preset drift, generator regressions, or download failures are detected only during manual bootstrap.
- Priority: High

**Async engine TODO paths and failure handling:**
- What's not tested: network reconciliation, deterministic seed setup, adaptive budget management, crash-dump service startup, and graphics backend invalidation handling.
- Files: `src/Oxygen/Engine/AsyncEngine.cpp`, `src/Oxygen/Engine/Test/AsyncEngine_api_test.cpp`
- Risk: runtime-only failures surface late and are hard to reproduce from unit coverage.
- Priority: High

**FrameContext surface removal and future shared game data:**
- What's not tested: removal-driven view cleanup and any conversion behavior once `GameData` stops being an empty placeholder.
- Files: `src/Oxygen/Core/FrameContext.cpp`, `src/Oxygen/Core/FrameContext.h`, `src/Oxygen/Engine/Test/FrameContext_test.cpp`
- Risk: stale presentation state and invalid references can slip through unnoticed.
- Priority: Medium

---

*Concerns audit: 2026-04-03*
