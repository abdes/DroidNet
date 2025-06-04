# Oxygen Engine Scene System

The Scene system is a sophisticated hierarchical scene graph implementation designed for high-performance 3D applications. It features efficient resource management, lazy evaluation, and a robust component-based architecture.

## Table of Contents
- [Architecture Overview](#architecture-overview)
- [Core Components](#core-components)
- [Design Patterns](#design-patterns)
- [Transform System](#transform-system)
- [Flag System](#flag-system)
- [Scene Update Cycle](#scene-update-cycle)
- [Resource Management](#resource-management)
- [Usage Examples](#usage-examples)
- [Performance Considerations](#performance-considerations)
- [API Reference](#api-reference)

## Architecture Overview

The Scene system is built around several key architectural principles:

- **Resource Table-Based Storage**: Efficient handle-based access with sparse/dense storage optimization
- **Handle/View Pattern**: Safe node access with automatic lifetime management
- **Component Composition**: Modular design with pluggable components
- **Lazy Evaluation**: Deferred computation for optimal performance
- **Hierarchical Flag Inheritance**: Efficient property propagation through the scene graph

### System Dependencies
```
Scene
├── ResourceTable (Base) - Handle-based resource management
├── SceneNode - Public API wrapper with Transform integration
├── SceneNodeImpl - Internal node data storage
├── TransformComponent - TRS transform system
└── SceneFlags - Hierarchical flag system
```

## Core Components

### Scene Class
The `Scene` class serves as the root container and manager for all scene nodes.

**Key Features:**
- Root node management with automatic hierarchy setup
- Resource table-based node storage (`shared_ptr<NodeTable>`)
- Node creation/destruction lifecycle management
- Scene-wide update coordination

**Core API:**
```cpp
class Scene {
public:
    SceneNode CreateNode(const std::string& name = "");
    void DestroyNode(SceneNode& node);
    SceneNode GetRootNode() const;
    void Update(); // Two-pass update cycle
};
```

### SceneNode Class
The `SceneNode` class provides the public interface for scene graph manipulation.

**Key Features:**
- Handle/view pattern with weak scene reference
- Integrated Transform wrapper for TRS operations
- Safe method calling with validation
- Parent-child hierarchy management

**Core API:**
```cpp
class SceneNode {
public:
    // Hierarchy Management
    void SetParent(const SceneNode& parent);
    SceneNode GetParent() const;
    std::vector<SceneNode> GetChildren() const;

    // Transform Access (delegated to TransformComponent)
    Transform& GetTransform();
    const Transform& GetTransform() const;

    // Metadata
    void SetName(const std::string& name);
    std::string GetName() const;
};
```

### SceneNodeImpl Class
Internal implementation containing the actual node data.

**Components:**
- `ObjectMetaData` - Name and identification
- `SceneNodeData` - Hierarchy relationships (parent/children)
- `TransformComponent` - TRS transformation data

## Design Patterns

### Handle/View Pattern
The system uses a sophisticated handle/view pattern for safe resource access:

```cpp
// SceneNode acts as a "view" into the actual data
SceneNode node = scene.CreateNode("MyNode");
// Underlying SceneNodeImpl stored in ResourceTable
// SceneNode holds weak_ptr to Scene + ResourceHandle
```

**Benefits:**
- Automatic lifetime management
- Safe access even after node destruction
- Efficient copying (handles are lightweight)
- Clear ownership semantics

### SafeCall Pattern
Critical operations are protected with the SafeCall pattern:

```cpp
template<typename Func>
auto SafeCall(Func&& func) const -> decltype(func()) {
    if (auto scene = m_scene.lock()) {
        if (auto impl = scene->m_nodeTable->Get(m_handle)) {
            return func();
        }
    }
    return decltype(func()){}; // Safe default
}
```

### Component Composition
Nodes are composed of discrete components rather than inheritance:

```cpp
struct SceneNodeImpl {
    ObjectMetaData metadata;
    SceneNodeData nodeData;
    TransformComponent transform;
};
```

## Transform System

The transform system implements a hierarchical TRS (Translation-Rotation-Scale) model with lazy evaluation.

### TRS Decomposition
```cpp
class TransformComponent {
    glm::vec3 m_translation{0.0f};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_scale{1.0f};

    mutable glm::mat4 m_localMatrix{1.0f};
    mutable glm::mat4 m_worldMatrix{1.0f};
    mutable bool m_localMatrixDirty = true;
    mutable bool m_worldMatrixDirty = true;
};
```

### Matrix Composition
Local matrices are computed using standard TRS composition:
```cpp
glm::mat4 localMatrix = glm::translate(glm::mat4(1.0f), translation) *
                       glm::mat4_cast(rotation) *
                       glm::scale(glm::mat4(1.0f), scale);
```

World matrices incorporate parent transformations:
```cpp
glm::mat4 worldMatrix = parentWorldMatrix * localMatrix;
```

### Lazy Evaluation
Matrix computation is deferred until needed:

1. **Dirty Tracking**: TRS changes mark matrices as dirty
2. **On-Demand Computation**: Matrices computed only when accessed
3. **Hierarchical Invalidation**: Parent changes invalidate child world matrices
4. **Caching**: Clean matrices are cached until next modification

### Hierarchy Updates
The transform system maintains parent-child relationships:

```cpp
void SetParent(ResourceHandle newParent) {
    // Update parent reference
    // Add to parent's children list
    // Mark world matrix as dirty (parent transform affects world space)
}
```

## Flag System

The Scene system includes a sophisticated 5-bit flag system for efficient property management.

### Bit Layout
Each flag uses 5 bits with specific semantics:
```cpp
// Bit layout (from LSB to MSB):
// Bit 0: Effective Value - The computed final value
// Bit 1: Inheritance - Whether value is inherited from parent
// Bit 2: Pending Value - The desired local value
// Bit 3: Dirty - Whether flag needs recomputation
// Bit 4: Previous Value - The last effective value (for change detection)
```

### Flag States
The system supports three logical states:
- **Explicit True/False**: Directly set local values
- **Inherited**: Value comes from parent node
- **Default**: System-defined fallback behavior

### Hierarchical Inheritance
Flags automatically propagate through the scene hierarchy:

```cpp
void UpdateFlags() {
    for (auto child : children) {
        if (child.IsInheriting(flag)) {
            child.SetEffectiveValue(flag, this->GetEffectiveValue(flag));
        }
    }
}
```

### Atomic Operations
The flag system supports thread-safe operations where needed:
```cpp
std::atomic<uint32_t> m_flags; // For concurrent access scenarios
```

## Scene Update Cycle

The Scene system uses a two-pass update cycle for optimal performance:

### Pass 1: Flag Processing
```cpp
void Scene::Update() {
    // Process all dirty flags first
    ProcessDirtyFlags(rootNode);

    // Then update transforms
    UpdateTransforms(rootNode);
}
```

### Pass 2: Transform Updates
Hierarchical transform updates propagate through the scene graph:

```cpp
void UpdateTransforms(SceneNode& node) {
    if (node.GetTransform().IsWorldMatrixDirty()) {
        node.GetTransform().ComputeWorldMatrix();
    }

    for (auto& child : node.GetChildren()) {
        UpdateTransforms(child);
    }
}
```

### Update Efficiency
- **Dirty Tracking**: Only modified nodes are updated
- **Lazy Evaluation**: Computations deferred until needed
- **Batch Processing**: Related updates processed together
- **Hierarchical Pruning**: Clean subtrees are skipped

## Resource Management

The Scene system uses the ResourceTable for efficient memory management.

### Sparse/Dense Storage
```cpp
template<typename T>
class ResourceTable {
    std::vector<T> m_denseArray;           // Actual data storage
    std::vector<uint32_t> m_sparseToLocal; // Handle → dense index
    std::vector<ResourceHandle> m_localToSparse; // Dense index → handle
};
```

### Handle-Based Access
```cpp
struct ResourceHandle {
    uint32_t index;    // Index into sparse array
    uint32_t version;  // Version for ABA problem prevention
};
```

**Benefits:**
- **Cache Friendly**: Dense storage for iteration
- **Stable Handles**: Handles remain valid across operations
- **Efficient Lookup**: O(1) access via sparse array
- **Memory Efficient**: No fragmentation from deletions

### Lifetime Management
- **Scene Ownership**: Scene holds `shared_ptr<NodeTable>`
- **Node References**: SceneNodes hold `weak_ptr<Scene>` + handle
- **Automatic Cleanup**: Nodes automatically detect scene destruction
- **Safe Access**: All operations validate scene and handle before proceeding

## Usage Examples

### Basic Scene Creation
```cpp
#include "Oxygen/Scene/Scene.h"

// Create a scene
auto scene = std::make_shared<Scene>();

// Create nodes
SceneNode root = scene->GetRootNode();
SceneNode child = scene->CreateNode("Child");

// Set up hierarchy
child.SetParent(root);

// Configure transforms
child.GetTransform().SetTranslation({1.0f, 2.0f, 3.0f});
child.GetTransform().SetRotation(glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)));

// Update the scene
scene->Update();
```

### Advanced Transform Manipulation
```cpp
// Create a complex hierarchy
SceneNode camera = scene->CreateNode("Camera");
SceneNode cameraArm = scene->CreateNode("CameraArm");
SceneNode target = scene->CreateNode("Target");

// Set up parent-child relationships
cameraArm.SetParent(target);
camera.SetParent(cameraArm);

// Configure transforms for orbit camera
target.GetTransform().SetTranslation({0.0f, 0.0f, 0.0f});
cameraArm.GetTransform().SetRotation(glm::angleAxis(orbitAngle, glm::vec3(0, 1, 0)));
camera.GetTransform().SetTranslation({0.0f, 0.0f, orbitDistance});

// Update to compute world matrices
scene->Update();

// Access final camera world matrix
glm::mat4 cameraWorld = camera.GetTransform().GetWorldMatrix();
```

### Safe Node Operations
```cpp
void ProcessNode(SceneNode node) {
    // All operations are automatically safe
    if (!node.IsValid()) {
        return; // Node was destroyed or scene invalid
    }

    // These calls use SafeCall pattern internally
    std::string name = node.GetName();
    glm::vec3 pos = node.GetTransform().GetTranslation();

    // Hierarchy operations are also safe
    for (auto child : node.GetChildren()) {
        ProcessNode(child); // Recursive processing
    }
}
```

## Performance Considerations

### Memory Layout
- **Cache-Friendly Storage**: ResourceTable uses dense arrays for iteration
- **Minimal Indirection**: Handle lookup is single-level indirection
- **Component Locality**: Related data stored together in SceneNodeImpl

### Computational Efficiency
- **Lazy Evaluation**: Expensive operations deferred until needed
- **Dirty Tracking**: Only changed nodes are updated
- **Batch Updates**: Scene-wide updates process related changes together
- **Hierarchical Pruning**: Clean subtrees skipped during updates

### Scalability
- **O(1) Node Access**: Handle-based lookup is constant time
- **O(n) Updates**: Update time scales linearly with dirty nodes
- **Memory Efficiency**: No fragmentation from node creation/destruction
- **Thread Safety**: Atomic flag operations where needed

### Optimization Guidelines
1. **Minimize Transform Changes**: Group related transform operations
2. **Update Batching**: Call `Scene::Update()` once per frame
3. **Hierarchy Design**: Keep frequently-updated nodes shallow in hierarchy
4. **Flag Usage**: Use inheritance for properties that naturally cascade

## API Reference

### Scene Class
```cpp
class Scene {
public:
    // Node Management
    SceneNode CreateNode(const std::string& name = "");
    void DestroyNode(SceneNode& node);
    SceneNode GetRootNode() const;

    // Scene Operations
    void Update();

    // Internal Access (for advanced usage)
    std::shared_ptr<NodeTable> GetNodeTable() const;
};
```

### SceneNode Class
```cpp
class SceneNode {
public:
    // Validation
    bool IsValid() const;

    // Hierarchy
    void SetParent(const SceneNode& parent);
    SceneNode GetParent() const;
    std::vector<SceneNode> GetChildren() const;
    void AddChild(const SceneNode& child);
    void RemoveChild(const SceneNode& child);

    // Metadata
    void SetName(const std::string& name);
    std::string GetName() const;

    // Transform Access
    Transform& GetTransform();
    const Transform& GetTransform() const;

    // Internal
    ResourceHandle GetHandle() const;
};
```

### Transform Class (TransformComponent wrapper)
```cpp
class Transform {
public:
    // Translation
    void SetTranslation(const glm::vec3& translation);
    const glm::vec3& GetTranslation() const;

    // Rotation
    void SetRotation(const glm::quat& rotation);
    const glm::quat& GetRotation() const;

    // Scale
    void SetScale(const glm::vec3& scale);
    const glm::vec3& GetScale() const;

    // Matrices (computed lazily)
    const glm::mat4& GetLocalMatrix() const;
    const glm::mat4& GetWorldMatrix() const;

    // Utility
    bool IsLocalMatrixDirty() const;
    bool IsWorldMatrixDirty() const;
};
```

## PENDING Development Items
- Add explicit deep clone API: `SceneNode::Clone(bool deep = true)`
- Update/add scenario-based Google Test cases for all new/changed APIs (Scene class only)

## Future Extension Points

The SceneNode wrapper provides natural places to add:

- **Animation Integration**: Transform keyframe interpolation
- **Coordinate Space Conversions**: World ↔ Local space utilities
- **Physics Integration**: Rigid body component attachment
- **Validation and Constraints**: Transform limits and validation rules
- **Serialization**: Scene save/load functionality
- **Multi-threading**: Parallel scene graph updates
- **Culling Integration**: Frustum and occlusion culling
- **Event System**: Node change notifications
