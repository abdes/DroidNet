# Oxygen Renderer Resources Module

## To-Be Design State

The Resources module is responsible for:

- Processing immutable `RenderItemData` arrays from ScenePrep (via snapshot)
- Deduplicating and allocating GPU resources (geometry, materials, transforms)
- Managing bindless handles and descriptor tables
- Assembling and freezing a `PreparedSceneFrame` for RenderGraph consumption
- Integrating with the Upload module for all GPU data transfers
- Providing a clean, testable, and phase-aligned API for resource management

### Key Components

- **ResourceCoordinator**: Orchestrates all resource processing and owns the workers
- **GeometryUploader**: Maps geometry assets to GPU buffers, interns mesh identities, schedules uploads
- **MaterialBinder**: Maps material assets to GPU buffers, manages bindless slots, schedules uploads
- **TransformUploader**: Deduplicates and uploads transform matrices
- **HandleAllocator**: Allocates stable, versioned handles
- **ResidencyTracker**: Tracks resource residency and upload needs
- **DescriptorManager**: Manages descriptor tables for bindless access

### Data Flow

1. ScenePrep produces immutable `RenderItemData` during kSnapshot
2. ResourceCoordinator processes these in kPostParallel
3. All GPU uploads are scheduled via UploadCoordinator
4. PreparedSceneFrame is assembled and frozen for RenderGraph
5. Legacy resource management is removed from ScenePrep and other modules

## Trackable Tasks

- [ ] 1. Document and finalize ScenePrep to Resources data contract
- [ ] 2. Create or refactor ResourceCoordinator API and responsibilities
- [ ] 3. Implement or refactor worker components (GeometryUploader, MaterialBinder, etc.)
- [ ] 4. Implement main processing function in ResourceCoordinator
- [ ] 5. Integrate with Upload module for GPU data transfers
- [ ] 6. Wire up phase participation for ResourceCoordinator
- [ ] 7. Remove or refactor legacy resource management
- [ ] 8. Write or update unit tests for Resources module
- [ ] 9. Document Resources module and update README

## Migration Notes

- Refactor is incremental; start with geometry, then materials, then transforms, then bindless/descriptor management
- Ensure all new code is robust, testable, and phase-aligned
- Remove legacy manager classes as new pipeline is adopted

## ScenePrep → Resources Data Contract

The interface between ScenePrep and Resources is defined by the immutable array of `RenderItemData` produced during the kSnapshot phase. This contract ensures:

- **Immutability:** `RenderItemData` is finalized and not mutated after snapshot publication.
- **Ownership:** ScenePrep owns the creation and population of `RenderItemData`. Resources only consumes it.
- **Required Fields:**
  - `lod_index`, `submesh_index`: LOD and submesh selection for draw.
  - `geometry`: `std::shared_ptr<const GeometryAsset>` (asset reference, not a handle).
  - `material`: `std::shared_ptr<const MaterialAsset>` (asset reference, not a handle).
  - `material_handle`: Populated by ScenePrep for transition, but Resources will migrate to handle-based access.
  - `world_bounding_sphere`: For culling/visibility.
  - `transform_handle`: Stable reference into TransformManager.
  - `cast_shadows`, `receive_shadows`: Rendering flags.
- **No GPU Resource Handles:** No GPU buffer or descriptor handles are present in `RenderItemData` at this stage.
- **No Mutable State:** All fields are value types or immutable shared_ptrs.

### Handoff Expectations

- ScenePrep must call `ResetFrameData()` at the start of each frame to clear per-frame state.
- After extraction, `collected_items` in `ScenePrepState` contains all `RenderItemData` for the frame.
- Resources module will consume this array (by const reference or move) during kPostParallel.
- Resources is responsible for deduplication, handle allocation, and GPU resource management based on these items.
- Any additional per-item metadata needed by Resources must be added to `RenderItemData` and documented here.

### Example Structure

```cpp
struct RenderItemData {
 std::uint32_t lod_index = 0;
 std::uint32_t submesh_index = 0;
 std::shared_ptr<const GeometryAsset> geometry;
 std::shared_ptr<const MaterialAsset> material;
 MaterialHandle material_handle { 0U };
 glm::vec4 world_bounding_sphere;
 TransformHandle transform_handle { 0U };
 bool cast_shadows = true;
 bool receive_shadows = true;
};
```

See `ScenePrep/RenderItemData.h` for the authoritative definition.

**This contract must be kept up to date as new fields or requirements are added.**

---
For details, see `new_design.md` and the current TODOs in the project tracker.
