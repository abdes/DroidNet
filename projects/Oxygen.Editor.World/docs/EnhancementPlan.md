# World Module Enhancement Plan

## Editor-Focused Scene Graph Integration

> [!NOTE]
> This plan focuses on editor-specific enhancements to leverage the Oxygen Engine's runtime scene graph capabilities. The goal is NOT to duplicate engine classes, but to provide the minimal domain model needed for editing, serialization, and UI binding within the editor.

---

## Background

The Oxygen.Editor.World module currently provides a basic scene hierarchy with:

- `Scene` - root container
- `SceneNode` - hierarchical entity with components
- `Transform` - position/rotation/scale component
- `GameComponent` - base for components
- `Category` - project categorization (appears misplaced)

The engine runtime ([Scene](file:///f:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Scene)) provides a sophisticated scene graph with:

- Full hierarchy management with parent/child/sibling navigation
- Component-based architecture (TransformComponent, RenderableComponent, Camera)
- SceneFlags system with inheritance and dirty tracking
- SceneQuery system for efficient graph queries
- SceneTraversal for hierarchical iteration
- Cross-scene node adoption and re-parenting
- World/local transform management with caching

---

## Gap Analysis

### 1. **Hierarchy Navigation** ✅ Completed

**Engine Has:**

- First-class parent/child/sibling navigation
- Root node management
- Hierarchy iteration (`GetFirstChild`, `GetNextSibling`, `GetPrevSibling`, `GetParent`)

**Editor Needs:**

- `SceneNode` currently lacks parent/children references
- No way to navigate hierarchy in memory
- Serialization only captures flat node list

**Impact:** Cannot build TreeView UI, cannot traverse hierarchy for operations

Status: ✅ Implemented — Scene nodes now expose `Parent` and `Children`, and scenes use `RootNodes` with nested (de)serialization.

---

### 2. **Scene Node Flags System** ❌ MISSING

**Engine Has:**

- `SceneFlags<T>` with 5-bit state per flag (effective, pending, dirty, inherited, previous)
- Flag inheritance from parent nodes
- Deferred dirty processing
- Predefined flags: `kVisible`, `kCastsShadows`, `kReceivesShadows`, `kRayCastingSelectable`, `kIgnoreParentTransform`, `kStatic`, `kDynamic`

**Editor Needs:**

- Boolean properties for each flag (`IsVisible`, `CastsShadows`, etc.)
- Flag inheritance model (inherit from parent vs. local override)
- PropertyChanged notifications when flags change
- JSON serialization of flag states

**Impact:** Cannot represent visibility, shadow casting, static/dynamic hints, or raycasting selectability

---

### 3. **Renderable Component** ❌ MISSING

**Engine Has:**

- `RenderableComponent` for geometry rendering
- LOD (Level of Detail) support with policies (Fixed, Distance, ScreenSpaceError)
- Submesh visibility and material override system
- Bounding box/sphere calculations
- `GeometryAsset` and `MaterialAsset` references

**Editor Needs:**

- `RenderableComponent : GameComponent`
- Reference to geometry asset (by path/GUID)
- LOD policy configuration (for serialization)
- Submesh visibility toggles
- Material override references
- Does NOT need runtime rendering logic

**Impact:** Cannot assign meshes, configure LOD, override materials, or toggle submesh visibility in editor

---

### 4. **Camera Support** ❌ MISSING

**Engine Has:**

- `PerspectiveCamera` and `OrthographicCamera` components
- Projection matrix configuration (FOV, aspect, near/far planes, orthographic size)
- Camera attachment API on `SceneNode`

**Editor Needs:**

- `CameraComponent : GameComponent` base class
- `PerspectiveCamera : CameraComponent` (FOV, aspect, near, far)
- `OrthographicCamera : CameraComponent` (size, near, far)
- Serialization of camera parameters
- Does NOT need view/projection matrix computation (engine handles that)

**Impact:** Cannot place cameras in scenes, configure camera parameters, or mark nodes as camera viewpoints

---

### 5. **Transform Enhancements** ⚠️ INCOMPLETE

**Engine Has:**

- Local transform (position, rotation, scale)
- World transform computation with parent hierarchy
- Transform dirty tracking for cache invalidation
- `LookAt`, `Translate`, `Rotate`, `Scale` operations
- Both local and world getters/setters

**Editor Has:**

- `Transform` with `Position`, `Rotation`, `Scale` (Vector3)

**Editor Needs:**

- Mark engine integration: `IsWorldTransformDirty` flag (read-only, synced from engine)
- Optional: Read-only `WorldPosition`/`WorldRotation`/`WorldScale` (computed by engine, cached in editor)
- Does NOT need: transform math logic (all in engine)

**Impact:** Editor can't show world-space positions for deeply nested objects, can't optimize dirty tracking

---

### 6. **Scene Query Capabilities** ❌ MISSING (Editor-Side)

**Engine Has:**

- `SceneQuery` for finding nodes by predicate, path, or batch queries
- Early termination optimizations
- Path-based queries with wildcards (`/World/**/Weapon`)

**Editor Needs:**

- LINQ-based scene graph queries (C# idiomatic)
-- Example: `scene.AllNodes.Where(n => n.Name.Contains("Enemy"))`
- Does NOT need: dedicated `SceneQuery` class (engine provides runtime queries)
- Could provide extension methods for common patterns

**Impact:** Less ergonomic editor-side searches, but engine can handle runtime queries

---

### 7. **Scene Hierarchy Tree** ⚠️ INCOMPLETE

**Current (prior to Phase 1):**

- Previously `Scene` used a flat `Nodes` collection and had no parent-child relationships exposed.

**Current (after Phase 1):**

- The editor now exposes `Scene.RootNodes`, `Scene.AllNodes`, and `SceneNode.Parent` / `SceneNode.Children`.
- Nested JSON (de)serialization and hierarchy manipulation APIs are implemented (see Phase 1 notes above).

**Needed:**

- `SceneNode.Parent : SceneNode?`
- `SceneNode.Children : ObservableCollection<SceneNode>`
- Hierarchy manipulation: `AddChild()`, `RemoveChild()`, `SetParent()`
- Root nodes as separate collection: `Scene.RootNodes`
- Serialization preserves hierarchy via parent references or nested structure

**Impact:** Cannot display tree UI, cannot perform hierarchical operations in editor

---

### 8. **Cross-Scene Operations** ❌ NOT NEEDED (Engine Handles)

**Engine Has:**

- `AdoptNode`, `AdoptHierarchy` for moving nodes between scenes

**Editor Needs:**

- Let engine handle cross-scene logic
- Editor just needs to serialize/deserialize independent scenes
- No need to replicate adoption logic

**Impact:** None - this is purely runtime concern

---

## Proposed Enhancements

### Phase 1: Hierarchy Foundation ✅ Completed

#### 1.1 Add Hierarchy to `SceneNode`

**New Properties:**

```csharp
// SceneNode.cs
public SceneNode? Parent { get; private set; }
public ObservableCollection<SceneNode> Children { get; }
```

**New Methods:**

```csharp
public void AddChild(SceneNode child)
public void RemoveChild(SceneNode child)
public void SetParent(SceneNode? newParent)
public IEnumerable<SceneNode> Descendants()
public IEnumerable<SceneNode> Ancestors()
```

**Serialization:**

- Option A: Serialize parent GUID reference
- Option B: Serialize nested JSON structure

**Rationale:** Essential for TreeView binding and hierarchical editing

Status: ✅ This phase has been implemented in the editor module (see src/Scene.cs, src/SceneNode.cs, src/Utils/SceneJsonConverter.cs and associated tests). The changes introduce `Scene.RootNodes`, `Scene.AllNodes`, parent/children relationships, and nested JSON (de)serialization.

---

#### 1.2 Update `Scene` Root Management

**Changes:**

```csharp
// Scene.cs
public ObservableCollection<SceneNode> RootNodes { get; } // replaces Nodes
public IEnumerable<SceneNode> AllNodes => RootNodes.SelectMany(r => r.DescendantsAndSelf());
```

**Rationale:** Clarify root vs. all nodes, support multi-root scenes

---

### Phase 2: Scene Flags System

#### 2.1 Create `SceneNodeFlags` Enum

**New Class:**

```csharp
// SceneNodeFlags.cs
[Flags]
public enum SceneNodeFlags : uint
{
    None = 0,
    Visible = 1 << 0,
    CastsShadows = 1 << 1,
    ReceivesShadows = 1 << 2,
    RayCastingSelectable = 1 << 3,
    IgnoreParentTransform = 1 << 4,
    Static = 1 << 5,
    Dynamic = 1 << 6,
}
```

**Rationale:** Match engine flag semantics, use C# [Flags] pattern

---

#### 2.2 Add Flag Properties to `SceneNode`

**New Properties:**

```csharp
// SceneNode.cs
public bool IsVisible { get; set; } = true;
public bool CastsShadows { get; set; }
public bool ReceivesShadows { get; set; }
public bool IsSelectable { get; set; } = true;
public bool IgnoreParentTransform { get; set; }
public bool IsStatic { get; set; }
```

**Considerations:**

- Each property raises `PropertyChanged` (Use CommunityToolkit MVVM annotations and C# 13 partial properties)
- Serializes as individual JSON booleans (not bitfield)
- **Inheritance logic deferred to engine** (editor just stores local values)

**Rationale:** Editor-friendly boolean properties, engine handles inheritance at runtime

---

### Phase 3: Renderable Component

#### 3.1 Create `RenderableComponent`

**New Class:**

```csharp
// RenderableComponent.cs
public partial class RenderableComponent : GameComponent
{
    public string? GeometryAssetPath { get; set; }
    public LodPolicy? LodPolicy { get; set; }
    // Submesh configuration (future)
}

public enum LodPolicyType { Fixed, Distance, ScreenSpaceError }

public record LodPolicy(LodPolicyType Type, int? FixedLodIndex = null, float[]? DistanceThresholds = null);
```

**Serialization:**

- Geometry path as string
- LOD policy as nested JSON object with discriminator

**Rationale:** Enable mesh assignment and LOD configuration in editor

---

#### 3.2 Material Override Support (Future)

**Deferred:**

- Material override system requires asset management infrastructure
- Can add `MaterialOverrides : Dictionary<int, string>` later

---

### Phase 4: Camera Components

#### 4.1 Create Camera Component Hierarchy

**New Classes:**

```csharp
// CameraComponent.cs
public abstract partial class CameraComponent : GameComponent
{
    public float NearPlane { get; set; } = 0.1f;
    public float FarPlane { get; set; } = 1000f;
}

// PerspectiveCamera.cs
public partial class PerspectiveCamera : CameraComponent
{
    public float FieldOfView { get; set; } = 60f;
    public float AspectRatio { get; set; } = 16f / 9f;
}

// OrthographicCamera.cs
public partial class OrthographicCamera : CameraComponent
{
    public float OrthographicSize { get; set; } = 10f;
}
```

**JSON Discriminators:**

```csharp
[JsonDerivedType(typeof(PerspectiveCamera), "PerspectiveCamera")]
[JsonDerivedType(typeof(OrthographicCamera), "OrthographicCamera")]
public abstract partial class CameraComponent : GameComponent { }
```

**Rationale:** Match engine camera architecture, enable camera editing in scenes

---

### Phase 5: Transform Enhancements

#### 5.1 Add World Transform Caching (Optional)

**New Properties:**

```csharp
// Transform.cs
[JsonIgnore]
public Vector3? WorldPosition { get; internal set; } // synced from engine

[JsonIgnore]
public bool IsWorldTransformDirty { get; internal set; } = true; // synced from engine
```

**Rationale:** Display world positions in inspector, optimize dirty tracking sync

**Note:** Engine computes these, editor just caches for display

---

### Phase 6: LINQ Extension Methods (Optional)

#### 6.1 Scene Graph Query Extensions

**New Class:**

```csharp
// SceneNodeExtensions.cs
public static class SceneNodeExtensions
{
    public static IEnumerable<SceneNode> DescendantsAndSelf(this SceneNode node)
    public static IEnumerable<SceneNode> Descendants(this SceneNode node)
    public static IEnumerable<SceneNode> Ancestors(this SceneNode node)
    public static IEnumerable<SceneNode> AncestorsAndSelf(this SceneNode node)
    public static SceneNode? FindByPath(this Scene scene, string path)
}
```

**Rationale:** C#-idiomatic queries, supplement engine's runtime SceneQuery

---

## Out of Scope

### 1. **SceneQuery Class**

- Engine provides runtime queries
- Editor uses LINQ for design-time searches
- No need to replicate C++ query architecture

### 2. **Transform Math**

- Engine computes world transforms
- Editor doesn't need matrix multiplication logic
- Just cache results synced from engine

### 3. **Dirty Tracking System**

- Engine manages dirty flags for rendering
- Editor only needs to expose dirty state for sync debugging

### 4. **Cross-Scene Adoption**

- Pure runtime concern
- Editor serializes scenes independently

### 5. **SceneTraversal Infrastructure**

- Engine provides visitor-based traversal
- Editor uses LINQ/IEnumerable-based iteration

---

## Verification Plan

### Unit Tests

1. **Hierarchy Tests**
   - Verify parent/child relationships
   - Test `AddChild`, `RemoveChild`, circular reference prevention
   - Check `Descendants()`, `Ancestors()` enumeration

2. **Serialization Tests**
   - Round-trip Scene with nested hierarchy
   - Verify flags, components, cameras serialize correctly
   - Test scene with multiple roots

3. **Flag Tests**
   - Verify flag property setters raise PropertyChanged
   - Test flag serialization/deserialization

4. **Component Tests**
   - Test renderable component creation
   - Verify camera component polymorphism
   - Check component collection JsonDerivedType resolution

### Integration with Engine

- Sync test: create scene in editor, load in engine runtime
- Verify engine recognizes flags, cameras, renderables
- Confirm transform sync from engine to editor

---

## Migration Notes

### JSON Format Changes

- **Before**: `Scene.Nodes` as flat array
- **After**: `Scene.RootNodes` with nested children or parent references

### Migration Strategy

1. No requirement for backward compatibility.
2. Update serialization to support new format.
3. Update client code using old format and properties.

---

## Summary

| Feature | Status | Priority | Complexity |
|---------|--------|----------|------------|
| Hierarchy Navigation | ✅ Completed | **High** | Medium |
| Scene Flags System | ❌ Missing | **High** | Low |
| Renderable Component | ❌ Missing | **High** | Medium |
| Camera Components | ❌ Missing | Medium | Low |
| Transform Enhancements | ⚠️ Partial | Low | Low |
| LINQ Extensions | ❌ Missing | Low | Low |

**Recommended Implementation Order:**

1. Hierarchy Foundation (Phase 1) — ✅ Completed
2. Scene Flags (Phase 2)
3. Camera Components (Phase 4) - simpler than renderables
4. Renderable Component (Phase 3)
5. Transform Enhancements (Phase 5)
6. LINQ Extensions (Phase 6)
