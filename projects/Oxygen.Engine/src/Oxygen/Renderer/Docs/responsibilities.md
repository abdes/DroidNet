# Responsibilities & Ownership

This document defines the explicit responsibility split among the primary
rendering-layer actors. It reflects the current implementation (see
`Renderer.h`, `RenderContext.h`, `RenderPass.h`, pass subclasses) and avoids
speculative features.

## Summary Table

| Component | Owns | Knows About | Does Not Do |
|-----------|------|-------------|-------------|
| ResourceRegistry (graphics) | GPU resource objects & registered views | Descriptor allocations (through allocator), view descriptions | Eviction policy, scene logic |
| RenderController (graphics) | Frame lifecycle state, descriptor allocator, registry | Queues, command recorder creation | High-level mesh caching |
| Renderer (engine) | MeshId->`MeshGpuResources` map, eviction policy | RenderController (weak), EvictionPolicy | Creating textures, descriptor table layout policy |
| EvictionPolicy (engine) | Access time metadata (e.g. last-used frames) | Current resource set (for selection) | Direct GPU object lifetime |
| RenderPass (engine) | Pass configuration state, last built PSO desc | `RenderContext` (read-only), resource transitions during Prepare | Global resource ownership, cross-pass mutation outside registration |
| DepthPrePass / ShaderPass | Specialised PSO desc build logic | Access to `RenderContext` draw lists & buffers | Managing mesh uploads |
| RenderContext (engine) | Per-frame shared data + typed pass registry | Renderer / RenderController pointers (set by Renderer) | Long-term storage of pass outputs |

## Lifetime & Ownership Rules

* Application owns: `RenderController` and `Renderer` (creates Renderer with
  weak reference to controller).
* Renderer owns: mesh GPU buffers it lazily creates and registers; eviction
  policy object.
* ResourceRegistry owns: registered GPU resources & view descriptors for its
  lifetime (scoped to RenderController / frame system).
* Pass objects own only their configuration; they borrow the per-frame
  `RenderContext`.
* `RenderContext` validity spans a single graph invocation; it is reset by
  `Renderer::PostExecute`.

See also: [gpu resource management](gpu_resource_management.md), [render pass
lifecycle](render_pass_lifecycle.md).

## Threading & Safety Notes

* Mesh uploads happen inside `EnsureMeshResources` using a transient
  `CommandRecorder` acquired from `RenderController`; upload buffers are kept
  alive via `DeferredObjectRelease`.
* Pass resource state transitions are explicit and confined to
  `PrepareResources`.
* `RenderContext` mutability is restricted to Renderer (setting pointers) and
  pass registration.

## Eviction Policy Contract

```text
OnMeshAccess(MeshId)  // called on successful resource usage or creation
SelectResourcesToEvict(current_map, current_frame) -> vector<MeshId>
OnMeshRemoved(MeshId) // notification after erase
```

The policy inspects IDs only (no deep mesh inspection) and must not hold owning
references to GPU buffers.

Cross-reference: [gpu resource management](gpu_resource_management.md).
