# Scene Graph Node Naming Requirements Analysis

## Summary

After comprehensive research across major game engines and systems, **most scene
graph implementations do NOT require unique node names**. Names are primarily
used for debugging, tooling, and human readability rather than as primary
identifiers for the scene system.

## Industry Analysis

### Major Game Engines

| Engine | Unique Names Required? | Primary Identity | Name Usage |
|--------|----------------------|------------------|------------|
| **Unity** | ❌ No | `InstanceID` (int) | Debugging, editor display, GameObject.Find() |
| **Unreal Engine** | ❌ No | `UObject*` pointer + `FName` | Blueprint display, debugging, FindActor() |
| **Godot** | ❌ No | `ObjectID` (64-bit) | Debugging, get_node() paths, editor |
| **Bevy ECS** | ❌ No | `Entity` (generational index) | Optional Name component for debugging |
| **OpenSceneGraph** | ❌ No | Node pointer | Optional for debugging and traversal |
| **Three.js** | ❌ No | Object reference | Optional for scene.getObjectByName() |

### Key Findings

1. **Handle-Based Identity**: Modern engines use handle/ID-based systems for
   primary node identity
2. **Names for Tooling**: Names serve debugging, editor display, and developer
   convenience
3. **Non-Unique by Design**: Multiple nodes can share names without issues
4. **Optional Names**: Many systems allow unnamed nodes entirely
5. **Search Convenience**: Names enable find-by-name functionality for scripting

## Oxygen Engine Current Implementation

Based on analysis of the Oxygen Engine codebase:

### Identity System

```cpp
// Primary identity is NodeHandle-based, not name-based
NodeHandle handle = scene->CreateNode("PlayerCharacter");
NodeHandle another = scene->CreateNode("PlayerCharacter"); // Same name, different identity
```

### Naming Characteristics

- ✅ **Non-unique names allowed**: Multiple nodes can have identical names
- ✅ **Handle-based identity**: `NodeHandle` provides unique identification
- ✅ **Names for debugging**: Used in test output and logging
- ✅ **Optional names**: Nodes can function without meaningful names
- ✅ **Cross-scene cloning**: Names preserved during `CreateHierarchyFrom()`

### Evidence from Tests

```cpp
// From Scene_reparent_test.cpp - demonstrates non-unique naming
auto root1 = scene->CreateNode("Root");
auto root2 = scene->CreateNode("Root"); // Same name, different handles
EXPECT_NE(root1.GetHandle(), root2.GetHandle()); // Different identities
```

## Recommended Naming Requirements

### Core Requirements

1. **Non-Unique Names**: Do not enforce name uniqueness within scenes
2. **Handle-Based Identity**: Use `NodeHandle` as primary identification
3. **UTF-8 String Names**: Support international characters
4. **Empty Names Allowed**: Nodes can have empty names
5. **Name Length Limits**: Reasonable limits (e.g., 256 characters)

### Best Practice Guidelines

#### Naming Conventions

```cpp
// Good naming practices
scene->CreateNode("Player");           // Clear, descriptive
scene->CreateNode("Enemy_Orc_01");     // Specific, numbered
scene->CreateNode("UI_HealthBar");     // Categorized
scene->CreateNode("LOD_Mesh_High");    // Purpose-specific

// Acceptable but less ideal
scene->CreateNode("Node_1234");        // Generic but functional
scene->CreateNode("");                 // Empty name (valid)
```

#### Hierarchical Naming

```cpp
// Hierarchical context provides clarity
Root: "GameWorld"
├── Child: "Player"          // Clear within game context
├── Child: "Environment"
│   ├── Child: "Tree"        // Clear within environment
│   └── Child: "Tree"        // Duplicate name OK - different handles
└── Child: "UI"
    └── Child: "Player"      // Duplicate "Player" name OK
```

### API Design Implications

#### Finding Nodes

```cpp
// Primary lookup should be handle-based
NodeHandle handle = /*...*/ ;
SceneNode node = scene->GetNode(handle);

// Name-based search as convenience (returns first match)
std::optional<SceneNode> found = scene->FindNodeByName("Player");

// Path-based search for hierarchical context
std::optional<SceneNode> specific = scene->FindNodeByPath("GameWorld/Player");
```

#### Cross-Scene Operations

```cpp
// Names preserved during cloning
SceneNode original = scene1->CreateNode("PlayerTemplate");
SceneNode cloned = scene2->CreateHierarchyFrom(original, "PlayerInstance");
// Both have different handles but meaningful names
```

## Implementation Recommendations

### 1. Validation Rules

```cpp
class NodeNaming {
public:
    static bool IsValidName(const std::string& name) {
        // Empty names allowed
        if (name.empty()) return true;

        // Length limit
        if (name.length() > 256) return false;

        // UTF-8 validation
        return IsValidUTF8(name);
    }

    static std::string SanitizeName(const std::string& name) {
        // Remove control characters, trim whitespace
        return SanitizeString(name);
    }
};
```

### 2. Search Functionality

```cpp
class Scene {
public:
    // Primary identity-based access
    SceneNode GetNode(NodeHandle handle) const;

    // Convenience search methods
    std::optional<SceneNode> FindFirstByName(const std::string& name) const;
    std::vector<SceneNode> FindAllByName(const std::string& name) const;
    std::optional<SceneNode> FindByPath(const std::string& path) const;

    // Pattern matching
    std::vector<SceneNode> FindByPattern(const std::regex& pattern) const;
};
```

### 3. Debugging Support

```cpp
class SceneDebugger {
public:
    // Generate unique display names for debugging
    std::string GetDisplayName(const SceneNode& node) const {
        auto impl = node.GetObject();
        if (!impl) return "<invalid>";

        std::string name = impl->GetName();
        if (name.empty()) name = "<unnamed>";

        return fmt::format("{}#{}", name, node.GetHandle().GetId());
    }

    // Hierarchical path for context
    std::string GetHierarchicalPath(const SceneNode& node) const;
};
```

## Cross-Scene Considerations

### Cloning and Migration

- **Preserve Names**: Original names should be preserved during cloning
- **Handle Remapping**: New handles assigned in target scene
- **Logical Equivalence**: Compare by name + hierarchy structure, not handles

### Serialization

- **Name Storage**: Include names in serialized scene data
- **Handle Regeneration**: Handles regenerated on load
- **Name-Based References**: External references by name should be robust

## Testing Implications

The current Oxygen Engine test framework already demonstrates good practices:

```cpp
// Identity-based comparison (current implementation)
EXPECT_NE(node1.GetHandle(), node2.GetHandle());

// Property-based comparison for content
builder_->ExpectEqual(expected_hierarchy, actual_hierarchy);

// Name-based display for debugging
std::cout << builder_->FormatAsTree(hierarchy) << std::endl;
```

## Conclusion

**Scene graphs should NOT require unique node names.** The Oxygen Engine's
current handle-based identity system with non-unique names aligns with industry
best practices. Names serve as human-readable labels for debugging and tooling,
while `NodeHandle` provides the robust identity system needed for scene
management.

This approach provides:

- ✅ **Flexibility**: Developers aren't constrained by naming conflicts
- ✅ **Performance**: Handle-based operations are efficient
- ✅ **Robustness**: System doesn't break due to naming issues
- ✅ **Usability**: Meaningful names aid development and debugging
- ✅ **Scalability**: Works well with large, complex scene graphs

The current Oxygen Engine implementation already follows these best practices
and should be maintained as-is.
