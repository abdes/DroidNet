# Renderer Module Completed Work

This document lists phases and tasks that are marked complete in the renderer roadmap. The living plan for remaining work is in `implementation_plan.md`.

---

## Phase 0 – Baseline Snapshot (Complete)

Goal: Solidify current minimal renderer (DepthPrePass + ShaderPass, mesh upload, LRU caching, typed pass registry).

Status: [x]

---

## Phase 1 – Constants & Bindless Indices API (Complete)

Goal: Move ad‑hoc constant buffer and DrawResourceIndices management out of the example into Renderer public API.

Tasks (all [x]):

- Define/Finalize `SceneConstants`; `Renderer::SetSceneConstants`.
- `MaterialConstants` + `Renderer::SetMaterialConstants`.
- Centralize bindless `DrawResourceIndices` in Renderer.
- Dirty tracking & upload before Execute.
- `bindless_conventions.md`: constants + indices buffer layout.
- `Renderer::PreExecute` validation for scene constants once per frame.
- Example migration; removed manual constants upload.
- Docs updates: `passes/data_flow.md`, `render_items.md` timing notes.

Deliverable: Example sets matrices via Renderer API only; no direct buffer mapping.

---

## Phase 2 – Mesh Resource & Bindless Abstraction (Implemented)

Goal: Eliminate manual SRV creation for vertex & index buffers in the example.

Tasks (all implemented):

- `Renderer::EnsureMeshResources(const data::Mesh&)` uploads and registers SRVs; stores shader indices in `MeshGpuResources`.
- Caching with LRU policy (keyed by object address for now).
- Descriptor allocator no longer exposed to example.
- Debug logging of assigned shader-visible indices.
- Example migration: removed Ensure* SRV paths.

Current status (2025-08-13):

- Mesh buffer creation, SRV registration, and caching implemented in Renderer.
- Per-frame `DrawResourceIndices` updated and bindless slot propagated.
- Example uses `EnsureMeshResources`.

Remaining doc work lives in `implementation_plan.md`.

---

## Phase 2.5 – Multi-Draw Item Support (Complete)

Tasks (all [x]):

- Root constant `kDrawIndexConstant` for per-draw index.
- Implement SetGraphics/ComputeRoot32BitConstant in CommandRecorder.
- `BindDrawIndexConstant()` on RenderPass base; bind before each draw.
- Shaders use `g_DrawIndex` instead of `SV_InstanceID`.
- Root signature layout consistent across passes.
- Renderer builds `DrawResourceIndices` for multiple items.
- Address D3D12 limitation with `SV_InstanceID`.

Result: Multiple meshes rendered with correct per-mesh resource binding.

---

## Phase 3 – RenderItem Validation & Container (Complete)

Tasks (all [x]):

- `RenderItemsList` with validation and auto `UpdateComputedProperties()`.
- `Renderer::GetOpaqueItems()` span for passes; PreExecute wires draw list.
- Example migrated to container; manual vector removed.
- Docs updated: container semantics & validation.

---

## Phase 4 – Scene Extraction Integration (Complete)

Tasks (all [x]):

- Types/Frustum and Types/View.
- `Extraction/SceneExtraction` with traversal, culling, and item build.
- `Renderer::BuildFrame(scene, view)` populates items and scene constants.
- Example migrated to BuildFrame; fallback retained during dev.
- Docs updated; unit tests for frustum and extraction.

Deliverable: Example uses camera-driven extraction; opaque-only pipeline renders with culling.
