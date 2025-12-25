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

### 2. **Scene Node Flags System** ✅ Completed

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

Status: ✅ Implemented — Editor exposes `SceneNodeFlags` enum (`src/SceneNodeFlags.cs`), helper conversions (`src/SceneNodeFlagsExtensions.cs`), and editor-local boolean flag properties plus a composed `Flags` property on `SceneNode` (`src/SceneNode.cs`).

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

### 4. **Camera Support** ✅ Completed

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

### Phase 0: Module Architecture Refactoring ✅ Completed

**Objective:** Separate the world model and project management concerns into distinct projects to improve modularity and maintainability.

**Changes:**

- Created `Oxygen.Editor.Projects` for project management logic (`ProjectManagerService`, `IProject`, etc.).
- Refactored `Oxygen.Editor.World` to focus solely on the world domain model (`Scene`, `SceneNode`, `Components`).
- Extracted document management to `Oxygen.Editor.Documents`.

Status: ✅ Implemented — The solution now has clear separation of concerns with `Oxygen.Editor.World`, `Oxygen.Editor.Projects`, and `Oxygen.Editor.Documents`.

---

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

### Phase 2: Scene Flags System ✅ Completed

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
public bool IsRayCastingSelectable { get; set; } = true;
public bool IgnoreParentTransform { get; set; }
public bool IsStatic { get; set; }
```

**Considerations:**

- Each property raises `PropertyChanged` (Use CommunityToolkit MVVM annotations and C# 13 partial properties)
- Serializes as individual JSON booleans (not bitfield)
- **Inheritance logic deferred to engine** (editor just stores local values)

### Phase 3: Persistence & Performance Optimization ✅ Completed

**Objective:** Ensure sub-second loading times for scenes with 1,000+ nodes by eliminating reflection overhead and event chatter during deserialization.

**Location:** All serialization classes reside in `src/Serialization` with namespace `Oxygen.Editor.World.Serialization`.

#### 3.1 Define Data Transfer Objects (Data)

**Why DTOs?**

1. **Source Generators:** `System.Text.Json` source generators produce highly optimized serialization code at compile time, but they require simple, acyclic types (POCOs). They struggle with complex classes like `SceneNode` (which has circular `Parent`/`Child` refs and `ObservableObject` logic).
2. **"Cold" Instantiation:** When mapping `Data -> Domain`, the new Domain objects have **no subscribers**. Triggering `PropertyChanged` on an object with no listeners is effectively a free operation (a simple null check).
3. **Maintainability:** DTOs decouple the serialized format from the internal domain logic.

**Concept:**

- **POCOs Only:** No `ObservableObject`, no events, no logic.
- **Flat Arrays:** Use `T[]` or `List<T>` instead of `ObservableCollection`.
- **Source Generation:** Use `[JsonSerializable]` for maximum performance.

```csharp
namespace Oxygen.Editor.World.Serialization;

// Serialization/NamedData.cs
public record NamedData
{
    public string Name { get; init; }
}

// Serialization/GameObjectData.cs
public record GameObjectData : NamedData
{
    public Guid Id { get; init; }
}

// Serialization/SceneData.cs
public record SceneData : GameObjectData
{
    public List<SceneNodeData> Nodes { get; init; } = [];
}

// Serialization/SceneNodeData.cs
public record SceneNodeData : GameObjectData
{
    // Components includes the node's Transform represented as a
    // TransformComponentData entry. All components (including Transform)
    // are carried in this list so component creation and hydration follow
    // a single, canonical code path.
    public List<ComponentData> Components { get; init; } = [];
    public List<OverrideSlotData> OverrideSlots { get; init; } = [];
    public List<SceneNodeData> Children { get; init; } = [];
}

// Serialization/ComponentData.cs
[JsonPolymorphic(TypeDiscriminatorPropertyName = "$type")]
[JsonDerivedType(typeof(GeometryComponentData), "GeometryComponent")]
public abstract record ComponentData : NamedData; // No ID for components

// Serialization/GeometryComponentData.cs
public record GeometryComponentData : ComponentData
{
    public string Geometry { get; init; }
    public List<OverrideSlotData> OverrideSlots { get; init; } = [];
    public List<TargetedOverrideData> TargetedOverrides { get; init; } = [];
}
```

#### 3.2 Update Domain Classes for Hydration

**Refined Class Hierarchy:**

1. **`INamed`**: Contract for having a `Name`.
2. **`IPersistent<TData>`**: Contract for `Hydrate(TData)` and `Dehydrate()`.
3. **`ScopedObservableObject`**: Base class implementing `INotifyPropertyChanged` with **RAII notification suppression**.
4. **`GameObject : ScopedObservableObject, INamed, IPersistent<GameObjectData>`** - **Has ID.**
5. **`GameComponent : ScopedObservableObject, INamed, IPersistent<ComponentData>`** - **No ID.**
6. **`SceneNode : GameObject`** - Inherits ID from GameObject.

Status: ✅ Implemented — The persistence layer has been fully refactored. We now use `ScopedObservableObject`, `INamed`, and `IPersistent<T>` interfaces. DTOs (`SceneData`, `SceneNodeData`, `ComponentData`, etc.) are defined in `Oxygen.Editor.World.Serialization`. The `SceneSerializer` handles the hydration/dehydration process, and `SceneNode` properly implements `Hydrate`/`Dehydrate`.

---

### Phase 4: Asset System Foundation (Lightweight) ✅ Completed

**Objective:** Establish the minimal infrastructure to reference assets (meshes, materials) in the editor, focusing on **built-in primitives** (Cube, Sphere, Plane) to unblock scene construction.

#### 4.1 Asset Naming & URI Scheme

We will use a **URI-based** naming convention where the first segment of the path corresponds directly to a **Mount Point** in the Content Browser.

**Format:** `asset:///<MountPoint>/<Path/To/Asset>`

| Mount Point | Description | Physical Source | Read-Only | Example |
| :--- | :--- | :--- | :--- | :--- |
| **`Content`** | User project assets. | `<ProjectRoot>/Content/` | ❌ No | `asset:///Content/Models/Hero.geo` |
| **`Engine`** | Built-in engine resources. | `oxygen.pak` | ✅ Yes | `asset:///Engine/Mesh/Cube` |
| **`Generated`** | Runtime procedural assets. | In-Memory | ✅ Yes | `asset:///Generated/Preview/Sphere` |
| **`<PakName>`** | External package content. | `<PakName>.pak` | ✅ Yes | `asset:///Oxygen.StandardAssets/Skybox` |

#### 4.2 Asset Resolution Architecture

To handle different asset sources (Files, PAKs, Memory) uniformly, we will use a **Strategy Pattern** for resolution.

**1. The Resolver Interface:**

```csharp
public interface IAssetResolver
{
    // Returns true if this resolver handles the given mount point
    bool CanResolve(string mountPoint);

    // Resolves the URI to an Asset object
    Task<Asset?> ResolveAsync(string uri);
}
```

**2. The Asset Service (Orchestrator):**

The `IAssetService` maintains a registry of resolvers and delegates requests based on the URI's mount point.

```csharp
public interface IAssetService
{
    void RegisterResolver(IAssetResolver resolver);
    Task<T?> LoadAssetAsync<T>(string uri) where T : Asset;
}
```

**3. Concrete Resolvers:**

- **`GeneratedAssetResolver`**: Handles `Generated` mount point.
  - *Mechanism:* Looks up assets in a thread-safe in-memory dictionary.
  - *Usage:* Used for built-in primitives and procedural content.
- **`FileSystemAssetResolver`**: Handles `Content` mount point.
  - *Mechanism:* Maps URI path to `<ProjectRoot>/Content` and deserializes files (JSON/Binary).
- **`PakAssetResolver`**: Handles `Engine` and Package mount points.
  - *Mechanism:* Uses `Oxygen.Content.PakFile` (via Interop) to read from `.pak` archives.

**4. Resolution Flow (External):**

1. **Consumer** (e.g., a Property Grid or Scene Loader) encounters an `AssetReference<T>`.
2. Consumer calls `IAssetService.LoadAssetAsync<T>(ref.Uri)`.
3. `AssetService` delegates to the appropriate `IAssetResolver`.
4. Consumer updates `ref.Asset` with the loaded result (optional caching).

#### 4.2 Define Asset Domain Models

**Classes:**

```csharp
// Assets/Asset.cs
public abstract class Asset
{
    // The canonical URI for this asset
    public string Uri { get; set; } // e.g., "asset:///Engine/Mesh/Cube"

    public string Name => System.IO.Path.GetFileNameWithoutExtension(Uri);
}

// Assets/GeometryAsset.cs
public class GeometryAsset : Asset
{
    // Matches Engine: GeometryAsset -> LODs (Meshes) -> SubMeshes
    public List<MeshLOD> Lods { get; set; } = [];
}

public class MeshLOD
{
    public int LodIndex { get; set; }
    public List<SubMesh> SubMeshes { get; set; } = [];
}

public class SubMesh
{
    public string Name { get; set; } // e.g., "Head", "Body"
    public int MaterialIndex { get; set; } // Default material index
}

// Assets/MaterialAsset.cs
public class MaterialAsset : Asset
{
    // Minimal material data
}
```

#### 4.3 Implement `AssetReference<T>`

**Class:**

```csharp
// Assets/AssetReference.cs
public class AssetReference<T> : ScopedObservableObject where T : Asset
{
    private string? uri;
    private T? asset;

    // The serialized reference to the asset
    public string? Uri
    {
        get => this.uri;
        set
        {
            // SetField already checks equality and returns true only if changed
            if (SetField(ref this.uri, value))
            {
                // Invalidate the asset when URI changes (unless it matches the current asset's URI)
                if (this.asset != null && this.asset.Uri != value)
                {
                    Asset = null;
                }
            }
        }
    }

    // Resolved metadata (runtime only, not serialized)
    [JsonIgnore]
    public T? Asset
    {
        get => this.asset;
        set
        {
            // SetField already checks equality and returns true only if changed
            if (SetField(ref this.asset, value))
            {
                // Sync URI when Asset is set
                if (value != null && Uri != value.Uri)
                {
                    Uri = value.Uri;
                }
            }
        }
    }
}
```

#### 4.4 Minimal `IAssetService`

**Interface:**

```csharp
public interface IAssetService
{
    // Registers a physical location for a given mount point
    void Mount(string mountPoint, string physicalPath);

    // Resolves URIs to Asset objects.
    Task<T?> LoadAssetAsync<T>(string uri) where T : Asset;
}
```

**Built-in Support (Phase 4 - Generated Only):**

For Phase 4, we will strictly use the **Generated** resolver for built-ins.

- `asset:///Generated/BasicShapes/Cube` -> GeometryAsset (1 LOD, 1 SubMesh "Main")
- `asset:///Generated/BasicShapes/Sphere` -> GeometryAsset (1 LOD, 1 SubMesh "Main")
- `asset:///Generated/BasicShapes/Plane` -> GeometryAsset (1 LOD, 1 SubMesh "Main")
- `asset:///Generated/BasicShapes/Cylinder` -> GeometryAsset (1 LOD, 1 SubMesh "Main")
- `asset:///Generated/Materials/Default` -> MaterialAsset

**Rationale:**

- **Future Proof:** The URI scheme scales to packages, remote assets, and generated content.
- **Consistent:** Uniform way to reference everything.
- **Extensible:** `IAssetService` can add handlers for new schemes (e.g., `http`) or mount points later.

#### 4.5 Project Structure & Implementation

**New Module:** `Oxygen.Assets`

**Dependencies:**

- `Oxygen.Core` (for `ScopedObservableObject`)
- `System.Text.Json` (for serialization)

**File Organization:**

```text
Oxygen.Assets/
├─ src/
│  ├─ Asset.cs                       # Abstract base class
│  ├─ GeometryAsset.cs               # Geometry + LODs + SubMeshes
│  ├─ MaterialAsset.cs               # Material metadata
│  ├─ AssetReference.cs              # AssetReference<T>
│  ├─ IAssetService.cs               # Service interface
│  ├─ IAssetResolver.cs              # Resolver strategy interface
│  └─ Resolvers/
│     ├─ GeneratedAssetResolver.cs   # In-memory resolver
│     ├─ FileSystemAssetResolver.cs  # Content folder resolver (stub for Phase 4)
│     └─ PakAssetResolver.cs         # PAK file resolver (stub for Phase 4)
└─ Oxygen.Assets.csproj
```

**Phase 4 Implementation Checklist:**

- [x] Create `Oxygen.Assets` project
- [x] Add reference to `Oxygen.Core`
- [x] Implement `Asset`, `GeometryAsset`, `MaterialAsset` (domain models)
- [x] Implement `AssetReference<T>` (using `ScopedObservableObject`)
- [x] Define `IAssetResolver` interface
- [x] Define `IAssetService` interface
- [x] Implement `GeneratedAssetResolver` with hardcoded built-ins:
  - `BasicShapes/Cube`, `BasicShapes/Sphere`, `BasicShapes/Plane`, `BasicShapes/Cylinder`
  - `Materials/Default`
- [x] Implement stub resolvers (`FileSystemAssetResolver`, `PakAssetResolver`)
- [x] Update `Oxygen.Editor.World` to reference `Oxygen.Assets`
- [x] Write unit tests for `AssetReference<T>` synchronization logic
- [x] Write unit tests for `GeneratedAssetResolver`

**Status:** ✅ **Completed** — The asset system foundation is fully implemented. The new `Oxygen.Assets` module provides URI-based asset references, a pluggable resolver architecture, and 5 built-in generated assets (4 primitive geometries + 1 default material). All 21 unit tests pass. `FileSystemAssetResolver` and `PakAssetResolver` are stubbed for future phases.

---

### Phase 5: Override Slots & Geometry System ✅ Completed

#### 5.1 Implement `OverridableProperty<T>`

**New Helper Type:**

```csharp
// Properties/OverridableProperty.cs
public readonly record struct OverridableProperty<T>(T DefaultValue, T OverrideValue, bool IsOverridden)
{
  public T EffectiveValue => IsOverridden ? OverrideValue : DefaultValue;

  // Instance helpers (factories moved to non-generic helper):
  public OverridableProperty<T> WithOverride(T value) => new(DefaultValue, value, true);
  public OverridableProperty<T> ClearOverride() => new(DefaultValue, default, false);

  // Convert to DTO when overridden
  public OverridablePropertyData<T>? ToDto() => IsOverridden ? new OverridablePropertyData<T> { OverrideValue = OverrideValue, IsOverridden = true } : null;
}

// Factory helpers now live in a non-generic helper:
```csharp
// Properties/OverridableProperty.Helper.cs
public static class OverridableProperty
{
  public static OverridableProperty<T> FromDefault<T>(T defaultValue) => new(defaultValue, default, false);
  public static OverridableProperty<T> FromDto<T>(T defaultValue, OverridablePropertyData<T>? dto)
    => dto is { IsOverridden: true } ? new(defaultValue, dto.OverrideValue, true) : FromDefault(defaultValue);
}
```

**Rationale:**

- **Value Semantics:** Behaves like a standard property (equatable, hashable).
- **Not Observable:** Changes are notified by the parent Slot raising `PropertyChanged` when the property is replaced.
- **Clean State:** Encapsulates the "Default vs Override" logic in a simple primitive.

#### 5.2 Define Override Slots

**Concept:**

- **OverrideSlots** are observable containers for `OverridableProperty<T>` values.
- **Universal:** They are attached to `SceneNode` and `GameComponent` via a standard `OverrideSlots` collection.
- **Polymorphic:** Serialized with type discriminators.

**New Classes:**

```csharp
// Slots/OverrideSlot.cs
[JsonPolymorphic(TypeDiscriminatorPropertyName = "$type")]
[JsonDerivedType(typeof(RenderingSlot), "RenderingSlot")]
[JsonDerivedType(typeof(LightingSlot), "LightingSlot")]
[JsonDerivedType(typeof(MaterialsSlot), "MaterialsSlot")]
[JsonDerivedType(typeof(LevelOfDetailSlot), "LevelOfDetailSlot")]
public abstract class OverrideSlot : ObservableObject
{
    // Base functionality
}

// Slots/RenderingSlot.cs
public class RenderingSlot : OverrideSlot
{
    private OverridableProperty<bool> _isVisible = OverridableProperty.FromDefault(defaultValue: true);
    public OverridableProperty<bool> IsVisible
    {
        get => _isVisible;
        set => SetProperty(ref _isVisible, value);
    }
}

// Slots/LightingSlot.cs
public class LightingSlot : OverrideSlot
{
    private OverridableProperty<bool> _castShadows = OverridableProperty.FromDefault(defaultValue: true);
    public OverridableProperty<bool> CastShadows
    {
        get => _castShadows;
        set => SetProperty(ref _castShadows, value);
    }

    private OverridableProperty<bool> _receiveShadows = OverridableProperty.FromDefault(defaultValue: true);
    public OverridableProperty<bool> ReceiveShadows
    {
        get => _receiveShadows;
        set => SetProperty(ref _receiveShadows, value);
    }
}

// Slots/LevelOfDetailSlot.cs
public class LevelOfDetailSlot : OverrideSlot
{
    private OverridableProperty<LodPolicy> _lod_policy = OverridableProperty.FromDefault<LodPolicy>(new FixedLodPolicy());
    public OverridableProperty<LodPolicy> LodPolicy
    {
        get => _lodPolicy;
        set => SetProperty(ref _lodPolicy, value);
    }
}

// Slots/MaterialsSlot.cs
public class MaterialsSlot : OverrideSlot
{
    // Manages material assignments per submesh
    private Dictionary<string, AssetReference<MaterialAsset>> _overrides = new();

    public void SetMaterial(int lod, int submesh, AssetReference<MaterialAsset> material);
    public AssetReference<MaterialAsset>? GetOverride(int lod, int submesh);
    public void ClearOverride(int lod, int submesh);
}
```

#### 5.3 Update `SceneNode` and `GameComponent`

**Base Integration:**

```csharp
// GameObject.cs (or IHasOverrideSlots interface)
public partial class GameObject
{
    // Standard container for all overrides (Global scope for this object)
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];

    // Helper to get/create a specific slot
    public T GetSlot<T>() where T : OverrideSlot, new() { ... }
}
```

#### 5.4 Update `GeometryComponent`

**Concept:**

- Supports overrides at three levels:
  1. **Component Level:** Applies to the entire geometry (via `OverrideSlots`).
  2. **Mesh Level:** Applies to a specific LOD.
  3. **Submesh Level:** Applies to a specific submesh within an LOD.

**New Classes:**

```csharp
// GeometryComponent.cs
public partial class GeometryComponent : GameComponent
{
    public AssetReference<GeometryAsset> Geometry { get; } = new();

    // Collection of targeted overrides (LOD/Submesh specific)
    public ObservableCollection<GeometryOverrideTarget> TargetedOverrides { get; } = [];
}

// GeometryOverrideTarget.cs
public class GeometryOverrideTarget : ObservableObject
{
    public int LodIndex { get; set; } = -1; // -1 = All LODs
    public int SubmeshIndex { get; set; } = -1; // -1 = All Submeshes

    // Slots applied specifically to this target
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];
}
```

**Refined Slots:**

- **`MaterialsSlot`** is simplified. It no longer manages a dictionary. It just holds a single `Material` property.
- When attached to a `GeometryOverrideTarget`, it overrides the material for that specific target.

```csharp
public class MaterialsSlot : OverrideSlot
{
    // The material to apply to the target(s)
    private OverridableProperty<AssetReference<MaterialAsset>> _material = ...;
    public OverridableProperty<AssetReference<MaterialAsset>> Material { ... }
}
```

#### 5.5 Persistence Format (Solid Spec)

**Structure:**

- `Components` list contains the entity definitions.
- `OverrideSlots` (Global) vs `TargetedOverrides` (Specific).

```json
{
  "$type": "Scene",
  "Name": "TestScene",
  "Id": "12345678-1234-1234-1234-123456789abc",
  "RootNodes": [
    {
      "$type": "SceneNode",
      "Name": "HeroNode",
      "Id": "abcdef12-1234-1234-1234-123456789abc",
      "Components": [
        {
          "$type": "Transform",
          "Name": "Transform",
          "Position": { "X": 0, "Y": 0, "Z": 0 },
          "Rotation": { "X": 0, "Y": 0, "Z": 0 },
          "Scale": { "X": 1, "Y": 1, "Z": 1 }
        },
        {
          "$type": "GeometryComponent",
          "Name": "HeroGeometry",
          "GeometryUri": "asset:///Content/Models/Hero.geo",
          "OverrideSlots": [
            {
              "$type": "LightingSlot",
              "CastShadows": {
                "OverrideValue": true,
                "IsOverridden": true
              }
            }
          ],
          "TargetedOverrides": [
            {
              "LodIndex": 0,
              "SubmeshIndex": 1,
              "OverrideSlots": [
                {
                  "$type": "MaterialsSlot",
                  "MaterialUri": "asset:///Content/Materials/Gold.mat"
                },
                {
                  "$type": "RenderingSlot",
                  "IsVisible": {
                    "OverrideValue": false,
                    "IsOverridden": true
                  }
                }
              ]
            }
          ]
        }
      ],
      "OverrideSlots": [
        {
          "$type": "LightingSlot",
          "CastShadows": {
            "OverrideValue": false,
            "IsOverridden": true
          }
        }
      ],
      "Children": [
        {
          "$type": "SceneNode",
          "Name": "Campfire",
          "Id": "fedcba98-4321-4321-4321-cba987654321",
          "Components": [
            {
              "$type": "Transform",
              "Name": "Transform",
              "Position": { "X": 0, "Y": 0, "Z": 0 }
            },
            {
              "$type": "GeometryComponent",
              "Name": "CampfireGeometry",
              "GeometryUri": "asset:///Content/Models/Props/Campfire.geo",
              "OverrideSlots": [
                {
                  "$type": "LightingSlot",
                  "CastShadows": {
                    "OverrideValue": true,
                    "IsOverridden": true
                  }
                }
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

---

### Phase 6: Camera Components ✅ Completed

#### 6.1 Create Camera Component Hierarchy (DTO-driven + Hydration)

In keeping with Phase 3 (Persistence & Hydration), camera components are serialized via DTOs (POCO records in the `Oxygen.Editor.World.Serialization` namespace) and hydrated into domain objects. Domain types implement `IPersistent<TData>` / `Hydrate(…)/Dehydrate()` so the scene serializer can perform fast, deterministic mapping.

##### Domain classes (editor model)

```csharp
// CameraComponent.cs (domain) - implements IPersistent<TData> for hydration
public abstract partial class CameraComponent : GameComponent, IPersistent<CameraComponentData>
{
  public float NearPlane { get; set; } = 0.1f;
  public float FarPlane { get; set; } = 1000f;

  public virtual void Hydrate(CameraComponentData data)
  {
    this.NearPlane = data.NearPlane;
    this.FarPlane = data.FarPlane;
  }

  public virtual CameraComponentData Dehydrate()
    => new CameraComponentData { NearPlane = this.NearPlane, FarPlane = this.FarPlane };
}

public partial class PerspectiveCamera : CameraComponent, IPersistent<PerspectiveCameraData>
{
  public float FieldOfView { get; set; } = 60f;
  public float AspectRatio { get; set; } = 16f / 9f;

  public override void Hydrate(CameraComponentData data)
  {
    base.Hydrate(data);
    if (data is PerspectiveCameraData pd)
    {
      this.FieldOfView = pd.FieldOfView;
      this.AspectRatio = pd.AspectRatio;
    }
  }

  public override PerspectiveCameraData Dehydrate()
    => new PerspectiveCameraData { NearPlane = this.NearPlane, FarPlane = this.FarPlane, FieldOfView = this.FieldOfView, AspectRatio = this.AspectRatio };
}

public partial class OrthographicCamera : CameraComponent, IPersistent<OrthographicCameraData>
{
  public float OrthographicSize { get; set; } = 10f;

  public override void Hydrate(CameraComponentData data)
  {
    base.Hydrate(data);
    if (data is OrthographicCameraData od)
      this.OrthographicSize = od.OrthographicSize;
  }

  public override OrthographicCameraData Dehydrate()
    => new OrthographicCameraData { NearPlane = this.NearPlane, FarPlane = this.FarPlane, OrthographicSize = this.OrthographicSize };
}
```

##### Serialization (DTO) types

The DTOs are simple POCO records suited for source-gen and fast JSON (de)serialization:

```csharp
// Oxygen.Editor.World.Serialization
public record CameraComponentData : ComponentData
{
  public float NearPlane { get; init; }
  public float FarPlane { get; init; }
}

public record PerspectiveCameraData : CameraComponentData
{
  public float FieldOfView { get; init; }
  public float AspectRatio { get; init; }
}

public record OrthographicCameraData : CameraComponentData
{
  public float OrthographicSize { get; init; }
}
```

##### JSON discriminators (DTOs)

Register the DTO variants with JsonDerivedType on the component-data base so source-gen produces polymorphic serializers for the DTO layer. The Scene/Serializer will read DTOs and hydrate domain objects.

```csharp
[JsonDerivedType(typeof(PerspectiveCameraData), "PerspectiveCamera")]
[JsonDerivedType(typeof(OrthographicCameraData), "OrthographicCamera")]
public abstract record CameraComponentData : ComponentData { }
```

**Rationale:** Keeps to Phase 3's model — DTOs are the serialized representation (POCO), domain objects are hydrated and implement IPersistent. This keeps serialization fast and avoids coupling JSON shape to runtime/instrumented domain objects.

---

### Phase 7: LINQ Extension Methods (Optional) ✅ Completed

#### 7.1 Scene Graph Query Extensions

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
| Scene Flags System | ✅ Completed | **High** | Low |
| Persistence & Performance | ✅ Completed | **High** | High |
| Asset System | ✅ Completed | **High** | Medium |
| Override Slots & Geometry | ✅ Completed | **High** | High |
| Camera Components | ✅ Completed | Medium | Low |
| LINQ Extensions | ✅ Completed | Low | Low |

**Recommended Implementation Order:**

1. Hierarchy Foundation (Phase 1) — ✅ Completed
2. Scene Flags (Phase 2) — ✅ Completed
3. Persistence & Performance (Phase 3) — ✅ Completed
4. Asset System Foundation (Phase 4) — ✅ Completed
5. Override Slots & Geometry (Phase 5)
6. Camera Components (Phase 6) — ✅ Completed
7. LINQ Extensions (Phase 7) — ✅ Completed
