# TestSceneFactory Developer Guide

## Introduction

The **TestSceneFactory** is your go-to tool for creating scene graphs in unit
tests. Whether you need a simple parent-child relationship or a complex game
object hierarchy, this factory provides both programmatic shortcuts and
JSON-based templates to get your tests up and running quickly.

## Why Use TestSceneFactory?

When testing scene graph functionality, you often need:
- **Predictable node hierarchies** for testing parent-child relationships
- **Consistent naming** that won't break when nodes are reparented
- **Complex structures** without writing hundreds of lines of setup code
- **Realistic scenarios** that mirror actual game object arrangements

The TestSceneFactory solves all these problems with a clean, fluent API and
powerful JSON templating system.

## Quick Start

### Basic Usage Pattern

```cpp
#include <Oxygen/Testing/GTest.h>
#include "TestSceneFactory.h"

class MySceneTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Clean slate for each test
    TestSceneFactory::Instance()
      .Reset()
      .SetDefaultCapacity(128)
      .GetNameGenerator()
      .SetPrefix("TestNode");
  }
};

TEST_F(MySceneTest, SimpleParentChild) {
  // Create a scene with one parent and one child
  auto scene = TestSceneFactory::Instance()
    .CreateParentChildScene("MyTest");

  EXPECT_EQ(scene->GetNodeCount(), 2);

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);
  EXPECT_TRUE(roots[0].HasChildren());
}
```

## Common Patterns

### 1. Single Node
Perfect for testing basic node operations:
```cpp
auto scene = TestSceneFactory::Instance()
  .CreateSingleNodeScene("SingleNodeTest");
```

### 2. Parent-Child Pair
Ideal for testing parent-child relationships:
```cpp
auto scene = TestSceneFactory::Instance()
  .CreateParentChildScene("ParentChildTest");
```

### 3. Multiple Children
Great for testing sibling relationships and iteration:
```cpp
auto scene = TestSceneFactory::Instance()
  .CreateParentWithChildrenScene("MultiChildTest", 3);
```

### 4. Linear Chain
Perfect for testing deep hierarchies and traversal:
```cpp
auto scene = TestSceneFactory::Instance()
  .CreateLinearChainScene("ChainTest", 5);  // 5 levels deep
```

### 5. Binary Tree
Excellent for testing complex traversal algorithms:
```cpp
auto scene = TestSceneFactory::Instance()
  .CreateBinaryTreeScene("TreeTest", 3);  // Depth of 3
```

## Name Generators: Your Testing Superpower

### Default Generator (Recommended)
Creates meaningful, context-aware names:
```cpp
// Automatically creates names like "Root", "Child0", "Child1"
TestSceneFactory::Instance()
  .GetNameGenerator()
  .SetPrefix("Player");
```

### Positional Generator
For predictable, sequential names:
```cpp
TestSceneFactory::Instance()
  .SetNameGenerator<PositionalNameGenerator>()
  .GetNameGenerator()
  .SetPrefix("Node");
// Creates: "NodeFirst", "NodeSecond", "NodeThird"
```

## JSON Templates: Complex Scenarios Made Easy

### Simple JSON Example
```cpp
const std::string scene_json = R"({
  "nodes": [
    {
      "name": "Player",
      "transform": {
        "position": [0, 1, 0],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1]
      },
      "children": [
        {
          "name": "Weapon",
          "transform": {
            "position": [0.5, 0.8, 0],
            "rotation": [0, 90, 0]
          }
        }
      ]
    }
  ]
})";

auto scene = TestSceneFactory::Instance()
  .CreateFromJson(scene_json, "PlayerTest");
```

### Template Registration
For reusable complex scenarios:
```cpp
// Register once
TestSceneFactory::Instance()
  .RegisterTemplate("player_setup", player_json);

// Use many times
auto scene1 = TestSceneFactory::Instance()
  .CreateFromTemplate("player_setup", "Test1");
auto scene2 = TestSceneFactory::Instance()
  .CreateFromTemplate("player_setup", "Test2");
```

## Best Practices for Test Writers

### 1. Always Reset in SetUp()
```cpp
void SetUp() override {
  TestSceneFactory::Instance().Reset();  // Clean slate
}
```

### 2. Use Descriptive Scene Names
```cpp
// Good - tells you what's being tested
auto scene = factory.CreateParentChildScene("ReparentingTest");

// Bad - generic name
auto scene = factory.CreateParentChildScene("Test");
```

### 3. Choose the Right Pattern
- **Single node**: Testing node properties, components
- **Parent-child**: Testing basic relationships
- **Multiple children**: Testing sibling operations
- **Linear chain**: Testing deep traversal, ancestor/descendant
- **JSON templates**: Testing complex, realistic scenarios

### 4. Validate What Matters
```cpp
TEST_F(MyTest, TestNodeCreation) {
  auto scene = TestSceneFactory::Instance()
    .CreateParentWithChildrenScene("ValidationTest", 2);

  // Test the structure
  EXPECT_EQ(scene->GetNodeCount(), 3);  // 1 parent + 2 children

  auto roots = scene->GetRootNodes();
  ASSERT_EQ(roots.size(), 1);

  auto parent = roots[0];
  EXPECT_TRUE(parent.HasChildren());
  EXPECT_FALSE(parent.HasParent());

  // Test the children
  auto first_child = parent.GetFirstChild();
  ASSERT_TRUE(first_child.has_value());
  EXPECT_TRUE(first_child->HasParent());
  EXPECT_FALSE(first_child->HasChildren());
}
```

### 5. Use Fluent Configuration
```cpp
// Chain configuration for readable test setup
auto scene = TestSceneFactory::Instance()
  .Reset()
  .SetDefaultCapacity(64)
  .SetNameGenerator<PositionalNameGenerator>()
  .CreateLinearChainScene("FluentTest", 4);
```

## Common Testing Scenarios

### Testing Node Relationships
```cpp
TEST_F(SceneGraphTest, ParentChildRelationships) {
  auto scene = TestSceneFactory::Instance()
    .CreateParentChildScene("RelationshipTest");

  auto roots = scene->GetRootNodes();
  auto parent = roots[0];
  auto child = parent.GetFirstChild().value();

  // Test bidirectional relationship
  EXPECT_EQ(child.GetParent().value(), parent);
  EXPECT_EQ(parent.GetFirstChild().value(), child);
}
```

### Testing Transform Inheritance
```cpp
TEST_F(TransformTest, LocalTransforms) {
  const std::string transform_json = R"({
    "nodes": [
      {
        "name": "Parent",
        "transform": { "position": [5, 0, 0] },
        "children": [
          {
            "name": "Child",
            "transform": { "position": [0, 3, 0] }
          }
        ]
      }
    ]
  })";

  auto scene = TestSceneFactory::Instance()
    .CreateFromJson(transform_json, "TransformTest");

  // Verify local transforms
  auto parent = scene->GetRootNodes()[0];
  auto child = parent.GetFirstChild().value();

  auto parent_pos = parent.GetTransform().GetLocalPosition();
  auto child_pos = child.GetTransform().GetLocalPosition();

  EXPECT_EQ(*parent_pos, glm::vec3(5, 0, 0));
  EXPECT_EQ(*child_pos, glm::vec3(0, 3, 0));
}
```

### Testing Node Operations
```cpp
TEST_F(NodeOperationsTest, Reparenting) {
  auto scene = TestSceneFactory::Instance()
    .CreateForestScene("ReparentTest", 2, 1);  // 2 trees, 1 child each

  auto roots = scene->GetRootNodes();
  auto tree1 = roots[0];
  auto tree2 = roots[1];
  auto child = tree1.GetFirstChild().value();

  // Test reparenting operation
  scene->ReparentNode(child, tree2);

  EXPECT_FALSE(tree1.HasChildren());
  EXPECT_TRUE(tree2.HasChildren());
  EXPECT_EQ(child.GetParent().value(), tree2);
}
```

## Error Handling and Validation

The TestSceneFactory provides robust error handling:

```cpp
TEST_F(ErrorHandlingTest, InvalidJson) {
  EXPECT_THROW(
    TestSceneFactory::Instance().CreateFromJson("invalid json", "ErrorTest"),
    std::invalid_argument
  );
}

TEST_F(ValidationTest, ValidateBeforeCreating) {
  const std::string json = "{ \"nodes\": [] }";

  auto error = TestSceneFactory::ValidateJson(json);
  EXPECT_FALSE(error.has_value());  // No error = valid JSON

  // Safe to create
  auto scene = TestSceneFactory::Instance()
    .CreateFromJson(json, "ValidatedTest");
}
```

## Performance Tips

### 1. Set Appropriate Capacity
```cpp
// For small test scenes
factory.SetDefaultCapacity(32);

// For large integration tests
factory.SetDefaultCapacity(1024);
```

### 2. Reuse Templates
```cpp
// Register once per test suite
static void SetUpTestSuite() {
  TestSceneFactory::Instance()
    .RegisterTemplate("complex_scene", complex_json);
}

// Use many times with different names
TEST_F(MyTest, Scenario1) {
  auto scene = factory.CreateFromTemplate("complex_scene", "Scenario1");
}
```

### 3. Choose Patterns Over JSON for Simple Cases
```cpp
// Faster - direct C++ creation
auto scene = factory.CreateLinearChainScene("FastTest", 5);

// Slower - JSON parsing overhead
auto scene = factory.CreateFromJson(chain_json, "SlowTest");
```

## Next Steps

- Check out `TestSceneFactory_example.cpp` for comprehensive usage examples
- Review `TestSceneFactory.schema.json` for the complete JSON schema
- See `TestSceneFactory.md` for detailed JSON documentation

The TestSceneFactory is designed to make your scene graph testing both powerful
and enjoyable. Start with the simple patterns, then graduate to JSON templates
as your testing needs become more sophisticated!
