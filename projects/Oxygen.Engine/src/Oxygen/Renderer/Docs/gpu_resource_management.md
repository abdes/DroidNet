# gpu resource management

Covers `Renderer` responsibility for mapping meshes (`data::Mesh`) to GPU
buffers plus eviction.

## Data Structures

```text
std::unordered_map<MeshId, MeshGpuResources> mesh_resources_;
struct MeshGpuResources { std::shared_ptr<Buffer> vertex_buffer; std::shared_ptr<Buffer> index_buffer; };
MeshId = bit_cast<std::size_t>(&Mesh);
```

Mesh identity uses the address of the mesh instance (current implementation). If
stable identifiers become necessary (asset streaming), adapt `GetMeshId`
accordingly.

## Creation Path

1. Caller asks `Renderer::GetVertexBuffer` / `GetIndexBuffer`.
2. `EnsureMeshResources`:
   * Look up `MeshId` → return if present (updates eviction policy
     `OnMeshAccess`).
   * Create vertex & index `Buffer` objects via
     `RenderController::GetGraphics().CreateBuffer(desc)`.
   * Register buffers with `ResourceRegistry`.
   * Acquire a transient `CommandRecorder` (graphics queue) and upload CPU data
     using staging (UPLOAD heap) → COPY → device-local state transitions.
   * Insert into map and notify eviction policy.

Uploads keep staging buffers alive by `DeferredObjectRelease(upload_buffer,
perFrameResourceManager)`.

## Eviction

```text
eviction_policy_->SelectResourcesToEvict(mesh_resources_, current_frame)
for each id → erase + eviction_policy_->OnMeshRemoved(id)
```

Current policy: LRU with frame-age threshold (default 60). Policy pluggable via
constructor parameter; if none provided a default LRU is instantiated.

## Policy Extensibility Considerations

* Frequency-based or memory-budget policies can re-use the same interface.
* Hybrid policy could inspect `mesh.VertexCount()` if needed (not implemented
  now).
* Ensure multi-threaded safety if eviction is moved off-thread (currently
  assumed single-threaded during frame orchestration).

## Failure Modes

| Failure | Handling |
|---------|----------|
| `RenderController` expired | Throw in `EnsureMeshResources` (fatal logic error) |
| Allocation failure (buffer creation) | Exception from graphics layer bubbles up |
| Upload with empty vertex/index data | Early return (no copy) |

Related: [responsibilities](responsibilities.md), [render pass
lifecycle](render_pass_lifecycle.md).
