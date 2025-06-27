# RenderItem, Scene, and Data System Integration Migration Tracker

This document tracks the step-by-step migration of the Oxygen Engine's
`RenderItem` and depth pre-pass system to full integration with the Scene and
Data (resource/asset) systems.

---

## Migration Steps & Progress Checklist

### **Step 1: RenderItem Interface and Data Ownership Principles**

- [x] **Define the RenderItem interface to be self-sufficient for rendering.**
  - The interface must provide all data required by the renderer or pre-passes
    to render the item, regardless of its origin (mesh, material, transform,
    state, etc.).
  - The renderer/pre-passes should not need to access the scene node or any
    external system to fetch additional data for rendering a RenderItem.
  - **Required data members for RenderItem:**
    - Mesh pointer: immutable, shared pointer to a `MeshAsset` (geometry and
      topology are provided by the asset).
    - Material pointer: immutable, shared pointer to a `MaterialAsset`.
    - Cached world transform matrix (`glm::mat4`) and normal transform (inverse
      transpose of world matrix).
    - Snapshotted SceneNode flags relevant for rendering: `cast_shadows`,
      `receive_shadows` (the `visible` flag is not included, as only visible
      items are included in the render list).
    - Optional render state: render layer (for pass selection/sorting), render
      flags (bitmask for custom per-item state).
    - Bounding sphere in world space (for fast culling; computed at render list
      construction from mesh bounds and transform).
  - **No reference to SceneNode or any mutable scene data is allowed in
    RenderItem.**
  - Only the minimal set of flag values that affect rendering must be
    snapshotted; flags used solely for scene management or logic are not
    included.
  - **RenderItem is a data-driven type:** It is implemented as a struct with
    public members and only the necessary convenience/helper methods. No
    unnecessary encapsulation or mutability is allowed. No legacy/demo fields or
    direct vertex/index storage are present.

- [x] **Decide on data ownership/caching policy for RenderItem:**
  - RenderItem owns or caches all required data (mesh/material pointers,
    transform, state) at the time of list construction. This ensures
    immutability and thread safety during rendering, and decouples the render
    pipeline from scene graph changes. The render list is an immutable snapshot
    for the frame render, allowing the scene to be updated in parallel.

- [x] **Document and justify the chosen policy.**
  - RenderItem is a snapshot of all renderable state at the time of list
    construction, and is immutable for the duration of the render pass. This is
    the best practice for game engines and enables safe, parallel scene updates
    and rendering.

**Deliverable:**

- The RenderItem interface and data members are designed to be self-sufficient
  for rendering, with a clear and documented policy for data ownership and
  caching.
- All code and documentation reflect this design, and the rendering pipeline is
  decoupled from scene graph internals.

---

### **Step 2: Integrate RenderItem with Scene Nodes**

- [ ] Implement the RenderItem construction logic to extract all required data
  from the owning SceneNode (or equivalent) at the time of list construction,
  according to the chosen policy above.
  - RenderItem must be constructed from a SceneNode (or equivalent), extracting
    all required data (mesh/material pointers, world matrix, render state, etc.)
    at construction time.
  - After construction, RenderItem must not reference the SceneNode or any
    mutable scene data; all required data must be cached or owned by the
    RenderItem.
- [ ] Remove or refactor any direct references to SceneNode from RenderItem if
  Option A is chosen.
- [ ] Ensure all transform logic in RenderItem uses the cached or owned data,
  not live queries to the scene graph.
- [ ] Update all usages and documentation to reflect that RenderItem is an
  immutable, self-sufficient snapshot for rendering.

**Note:** This approach enables thread safety and allows scene updates and
rendering to occur in parallel, as RenderItem is immutable and decoupled from
the scene graph after construction.

**Deliverable:** Every RenderItem is a self-sufficient, immutable (for the
frame) representation of a renderable entity, constructed from the scene system,
and ready for use by the renderer or pre-passes. No RenderItem may reference or
depend on live SceneNode data after construction.

---

### **Step 3: Implement Scene Traversal and Render List Generation**

- [ ] Implement a method in the scene system (e.g., `CollectRenderItems()`) that
  traverses the scene graph, applies culling, and returns a list of
  pointers/handles to visible `RenderItem` objects.
- [ ] Remove manual draw list assembly from the example and pipeline code.
- [ ] Update the depth pre-pass and other passes to use the scene-generated draw
  list.
- [ ] Ensure culling and render list generation occur before any rendering pass,
  and that the render list is immutable for the duration of the frame.

**Deliverable:**
The draw list for each render pass is generated by the scene system, not by
hand, and is an immutable, pre-culled snapshot for the frame.

---

### **Step 4: Integrate with Data/Resource System**

- [ ] Implement or connect to a resource manager/asset system for meshes,
  materials, and textures.
- [ ] Update `RenderItem` and scene node creation to request resources from the
  data system, not to create them directly.
- [ ] Ensure resource lifetime is managed by the data system, not by
  `RenderItem` or scene nodes.

**Deliverable:**
All resource references in `RenderItem` and scene nodes are managed by the data
system.

---

### **Step 5: Update Serialization and Streaming**

- [ ] Update scene and data system serialization to handle resource handles and
  scene node relationships.
- [ ] Ensure that loading a scene restores all `RenderItem` and resource
  references correctly.

**Deliverable:**
Scenes and renderable objects can be serialized/deserialized with all resource
and scene links intact.

---

### **Step 6: Refactor Example and Test Code**

- [ ] Update example code to create scene nodes/entities, attach components, and
  let the scene system generate draw lists.
- [ ] Update or add tests to verify correct integration and behavior.
- [ ] Add unit and integration tests to verify that RenderItem construction,
  immutability, and usage are correct, and that the render list is properly
  generated and consumed by the renderer.

**Deliverable:**
All examples and tests use the new architecture; no manual draw list or direct
geometry/material creation remains. RenderItem construction and usage are
validated by tests.

---

### **Step 7: Performance and Robustness Validation**

- [ ] Profile scene traversal, culling, and resource management.
- [ ] Test with large scenes and complex resource graphs.
- [ ] Fix any performance or correctness issues found.

**Deliverable:**
The integrated system is robust, efficient, and ready for production use.

---

## Progress Notes

- Use the checkboxes above to track completion of each subtask.
- Add notes, blockers, or design decisions below each step as needed.
- Update this document as the migration proceeds.

---

- [Migration Plan Discussion](#)
- [RenderItem.h](src/Oxygen/Graphics/Common/RenderItem.h)
- [Scene System Documentation](#)
- [Data System Documentation](#)

---

## Design Decisions

- The immutable snapshot approach for RenderItem is chosen to maximize thread
  safety, decouple rendering from scene updates, and enable parallelism.
- By extracting and caching all required data at construction, RenderItem
  becomes independent of the scene graph, preventing data races and simplifying
  the renderer's logic.
- This design supports efficient, multi-threaded rendering and culling, and is a
  proven pattern in modern game engines.
- The render list, as a flat, pre-culled, immutable collection, enables robust
  and scalable rendering pipelines, and allows the scene system to evolve
  independently of the renderer.
- The approach also simplifies serialization, streaming, and resource
  management, as all dependencies are explicit and managed by the data system.
