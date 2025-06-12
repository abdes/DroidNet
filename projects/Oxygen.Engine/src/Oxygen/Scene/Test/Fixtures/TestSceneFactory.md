# TestSceneFactory JSON Schema Documentation

## Overview

The TestSceneFactory supports creating complex scene graphs from JSON templates.
This document provides comprehensive documentation for the JSON schema, usage
patterns, and best practices.

## Schema Location

- **Schema File**: `TestSceneFactory.schema.json`
- **Schema ID**: `https://oxygen.engine/schemas/test-scene-factory.json`
- **Version**: 1.0

## Basic Structure

```json
{
  "metadata": { /* Optional template metadata */ },
  "nodes": [ /* Array of root-level scene nodes */ ]
}
```

## Core Concepts

### Node Hierarchy

- **Root Nodes**: Top-level nodes in the `nodes` array become scene roots
- **Children**: Each node can have a `children` array creating hierarchical
  relationships
- **Transforms**: All transforms are **local** (relative to parent)
- **Naming**: Names can be explicit or auto-generated using the current
  `NameGenerator`

### Coordinate System

- **Right-handed coordinate system**
- **Y-up orientation** (Y+ is up, Z+ is forward, X+ is right)
- **Units**: Positions in meters, rotations in degrees, scale as factors

### Transform Order

Transforms are applied in the standard order:
1. **Scale** - Applied first
2. **Rotation** - Applied second (Euler angles: X, Y, Z order)
3. **Translation** - Applied last

## Schema Reference

### Root Object

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `nodes` | array | ✓ | Array of root scene nodes |
| `metadata` | object | ✗ | Optional template metadata |
| `version` | string | ✗ | Schema version for compatibility |

### SceneNode Object

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `name` | string | ✗ | Explicit node name (auto-generated if omitted) |
| `transform` | object | ✗ | Local transformation data |
| `flags` | object | ✗ | Node behavior flags |
| `children` | array | ✗ | Child nodes array |

### Transform Object

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `position` | [x, y, z] | [0, 0, 0] | Local position in meters |
| `rotation` | [x, y, z] | [0, 0, 0] | Euler angles in degrees |
| `scale` | [x, y, z] | [1, 1, 1] | Scale factors |

### NodeFlags Object

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `visible` | boolean | true | Visibility flag (implemented) |
| `static` | boolean | false | Static hint (reserved) |
| `castsShadows` | boolean | true | Shadow casting (reserved) |
| `receivesShadows` | boolean | true | Shadow receiving (reserved) |

## Usage Examples

### 1. Simple Single Node

```json
{
  "metadata": {
    "name": "Basic Test",
    "description": "Single node for basic testing"
  },
  "nodes": [
    {
      "name": "TestRoot",
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1]
      }
    }
  ]
}
```

### 2. Parent-Child Hierarchy

```json
{
  "nodes": [
    {
      "name": "Parent",
      "transform": {
        "position": [0, 1, 0]
      },
      "children": [
        {
          "name": "Child",
          "transform": {
            "position": [1, 0, 0],
            "scale": [0.5, 0.5, 0.5]
          }
        }
      ]
    }
  ]
}
```

### 3. Complex Game Object

```json
{
  "nodes": [
    {
      "name": "Player",
      "transform": {
        "position": [0, 0, 0]
      },
      "flags": {
        "visible": true,
        "static": false
      },
      "children": [
        {
          "name": "Body",
          "transform": {
            "position": [0, 1, 0]
          }
        },
        {
          "name": "WeaponMount",
          "transform": {
            "position": [0.5, 1.5, 0],
            "rotation": [0, 90, 0]
          },
          "children": [
            {
              "name": "Sword",
              "transform": {
                "position": [0, 0, 0.3],
                "rotation": [45, 0, 0]
              }
            }
          ]
        }
      ]
    }
  ]
}
```

### 4. Auto-Generated Names

```json
{
  "nodes": [
    {
      "name": "Container",
      "children": [
        {
          "transform": { "position": [1, 0, 0] }
        },
        {
          "transform": { "position": [2, 0, 0] }
        },
        {
          "transform": { "position": [3, 0, 0] }
        }
      ]
    }
  ]
}
```

Names will be auto-generated based on the current `NameGenerator`:
- `DefaultNameGenerator`: "Child0", "Child1", "Child2"
- `PositionalNameGenerator`: "First", "Second", "Third"

### 5. Multiple Root Nodes

```json
{
  "nodes": [
    {
      "name": "Player",
      "transform": { "position": [0, 0, 0] }
    },
    {
      "name": "Environment",
      "transform": { "position": [10, 0, 0] }
    },
    {
      "name": "UI",
      "transform": { "position": [0, 5, 0] }
    }
  ]
}
```

## Integration with C++ API

### Factory Access

```cpp
// Access singleton instance
auto& factory = TestSceneFactory::Instance();

// Reset factory to clean state
factory.Reset();
```

### Name Generator Configuration

```cpp
// Set custom name generator with type safety
factory.SetNameGenerator<PositionalNameGenerator>();

// Set name generator with arguments
factory.SetNameGenerator<DefaultNameGenerator>();

// Configure current generator
factory.GetNameGenerator().SetPrefix("Custom");

// Reset to default generator
factory.ResetNameGenerator();
```

### Scene Creation Methods

```cpp
// Direct JSON creation
const std::string scene_json = R"({
  "nodes": [
    { "name": "Root", "transform": { "position": [0, 0, 0] } }
  ]
})";
auto scene = factory.CreateFromJson(scene_json, "TestScene", 1024);

// Pattern-based creation (no template registration needed)
auto single_node = factory.CreateSingleNodeScene("SingleTest");
auto parent_child = factory.CreateParentChildScene("ParentChildTest");
auto chain = factory.CreateLinearChainScene("ChainTest", 5);
auto tree = factory.CreateBinaryTreeScene("TreeTest", 3);
auto forest = factory.CreateForestScene("ForestTest", 3, 2);
```

### Configuration

```cpp
// Set default capacity for pattern-based scenes
factory.SetDefaultCapacity(2048);

// Get current capacity setting
auto capacity = factory.GetDefaultCapacity(); // returns std::optional<std::size_t>
```

### JSON Validation

```cpp
// Validate JSON without creating scene
auto error = TestSceneFactory::ValidateJson(json_string);
if (error.has_value()) {
    std::cerr << "Validation error: " << *error << std::endl;
}

// Get embedded schema for external tools
auto schema = TestSceneFactory::GetJsonSchema();
```

### Error Handling

```cpp
try {
    auto scene = factory.CreateFromJson(invalid_json);
    // Success
} catch (const std::invalid_argument& e) {
    // Handle validation/parsing errors
    std::cerr << "JSON Error: " << e.what() << std::endl;
}
```

## Best Practices

### 1. Naming Conventions

- **Explicit names**: Use for important/referenced nodes
- **Auto-generated**: Use for procedural/temporary nodes
- **Descriptive**: Choose names that indicate purpose
- **Consistent**: Use consistent naming schemes within templates

### 2. Transform Organization

- **Local transforms**: Always specify transforms relative to parent
- **Reasonable scales**: Avoid extreme scale values (< 0.01 or > 100)
- **Degree precision**: Use reasonable precision for rotations
- **Zero defaults**: Omit transform properties that are zero/identity

### 3. Hierarchy Design

- **Logical grouping**: Group related objects under common parents
- **Transform inheritance**: Use hierarchy for natural transform relationships
- **Depth limits**: Avoid excessively deep hierarchies (> 10 levels)
- **Balanced trees**: Prefer balanced over linear hierarchies for performance

### 4. Template Organization

- **Metadata**: Always include meaningful metadata
- **Modularity**: Create reusable sub-templates
- **Validation**: Test templates with schema validation
- **Documentation**: Comment complex transform calculations

### 5. Performance Considerations

- **Node count**: Be mindful of total node counts in large scenes
- **Transform complexity**: Simple transforms perform better
- **Hierarchy depth**: Deeper hierarchies impact transform updates
- **Auto-generation**: Use auto-generated names for large procedural content

## Error Handling

### Common Errors

1. **Invalid JSON syntax**: Malformed JSON structure
2. **Schema violations**: Missing required properties or invalid types
3. **Transform issues**: Invalid vector sizes or extreme values
4. **Circular references**: Nodes referencing themselves (future consideration)

### Error Messages

The factory provides detailed error messages for:
- JSON parsing errors with line/column information
- Schema validation failures with property paths
- Runtime creation failures with node context

## Validation Tools

### JSON Schema Validation

Use the provided schema file with JSON schema validators:

```bash
# Example with ajv-cli
npm install -g ajv-cli
ajv validate -s TestSceneFactory.schema.json -d your-scene.json
```

### C++ Validation

```cpp
try {
    auto scene = TestSceneFactory::Instance().CreateFromJson(json_string);
    // Success
} catch (const std::invalid_argument& e) {
    // Handle validation/parsing errors
    std::cerr << "JSON Error: " << e.what() << std::endl;
}
```

## Future Extensions

The schema is designed for extensibility:

- **Components**: Reserved for future component attachment
- **Advanced flags**: Additional node behavior flags
- **Animation**: Transform animation keyframes
- **Physics**: Physics properties and constraints
- **Rendering**: Material and mesh references

## Version Compatibility

- **Current version**: 1.0
- **Backward compatibility**: Maintained for all 1.x versions
- **Migration**: Automatic migration tools for major version updates
- **Deprecation**: 6-month notice for removed features

## API Notes

### Singleton Pattern
- TestSceneFactory uses the singleton pattern
- Access via `TestSceneFactory::Instance()`
- Thread-safe initialization (C++11 guarantees)

### Method Chaining
- Most configuration methods return `TestSceneFactory&` for chaining
- Example: `factory.Reset().SetDefaultCapacity(1024).ResetNameGenerator()`

### Template System
- **Removed**: Template registration and `CreateFromTemplate` are not part of
  the current API
- Use `CreateFromJson` for all JSON-based scene creation
- Pattern-based methods available for common structures

### Memory Management
- Factory returns `std::shared_ptr<Scene>` for automatic lifetime management
- Name generators managed internally via `std::unique_ptr`
- No manual memory management required

## Migration Notes

If upgrading from earlier versions:
- Template registration methods have been removed
- Use `CreateFromJson` instead of `CreateFromTemplate`
- Pattern-based creation methods provide common structures without JSON
