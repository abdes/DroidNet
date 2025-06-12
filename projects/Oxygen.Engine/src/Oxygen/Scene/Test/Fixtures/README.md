# Scene Testing Infrastructure

This directory contains reusable testing infrastructure for the Scene module, designed to reduce code duplication and provide consistent testing patterns across all Scene test files.

## Overview

The Scene module tests were experiencing significant code duplication (>50%) with repeated:
- Fixture setup/teardown code
- Node creation and validation helpers
- Common scene hierarchy setups
- Assertion and expectation helpers

This infrastructure centralizes these common patterns into reusable fixtures and helpers.

## Core Fixtures

### 1. SceneTest (Base Fixture)
**File**: `SceneTest.h`
**Purpose**: Core testing infrastructure for basic Scene functionality

**Key Features**:
- Automatic scene setup/teardown
- Node creation helpers with various flag configurations
- Comprehensive assertion helpers for validation
- Common scene hierarchy creation (parent-child, three-level, etc.)
- Transform manipulation helpers
- Categorized sub-fixtures (BasicTest, ErrorTest, DeathTest, etc.)

**Usage**:
```cpp
#include <Oxygen/Scene/Test/Fixtures/SceneTest.h>

class MySceneTest : public oxygen::scene::testing::SceneTest::BasicTest {
  // Your tests here
};

NOLINT_TEST_F(MySceneTest, ExampleTest)
{
  auto node = CreateNode("TestNode");
  ExpectNodeValidWithName(node, "TestNode");
  ExpectSceneNodeCount(1);
}
```

### 2. SceneTraversalTest
**File**: `SceneTraversalTest.h`
**Purpose**: Specialized infrastructure for scene traversal testing

**Key Features**:
- Automatic SceneTraversal setup
- Visitor and filter creation helpers
- Standard test hierarchies for traversal
- Visit tracking and validation helpers
- Performance testing support

**Usage**:
```cpp
#include <Oxygen/Scene/Test/Fixtures/SceneTraversalTest.h>

class MyTraversalTest : public oxygen::scene::testing::SceneTraversalTest::BasicTraversalTest {
  // Your traversal tests here
};
```

### 3. SceneFlagsTest
**File**: `SceneFlagsTest.h`
**Purpose**: Template-based infrastructure for SceneFlags testing

**Key Features**:
- Template-based for any flag enumeration
- Flag manipulation and validation helpers
- Inheritance testing support
- Dirty flag tracking and validation
- Common flag scenarios (all true, all false, mixed)

**Usage**:
```cpp
#include <Oxygen/Scene/Test/Fixtures/SceneFlagsTest.h>

class MyFlagsTest : public oxygen::scene::testing::TestSceneFlagsTest::BasicFlagsTest {
  // Your flags tests here
};
```

### 4. SceneCloningTest
**File**: `SceneCloningTest.h`
**Purpose**: Infrastructure for scene cloning and serialization testing

**Key Features**:
- Dual-scene setup (source and destination)
- Node equivalence validation
- Hierarchy cloning validation
- Transform preservation checking
- Serialization testing foundation

## Convenience Header

**File**: `SceneTestFixtures.h`
**Purpose**: Single include for all testing fixtures

**Usage**:
```cpp
#include <Oxygen/Scene/Test/Fixtures/SceneTestFixtures.h>

using namespace oxygen::scene::testing;

class MyTest : public fixtures::BasicTest {
  // All fixtures available via 'fixtures::' namespace
};
```

## Common Patterns

### 1. Basic Node Testing
```cpp
NOLINT_TEST_F(MyBasicTest, CreateAndValidateNode)
{
  // Create node with helper
  auto node = CreateNode("TestNode");

  // Validate with built-in helper
  ExpectNodeValidWithName(node, "TestNode");
  ExpectSceneNodeCount(1);
}
```

### 2. Hierarchy Testing
```cpp
NOLINT_TEST_F(MyBasicTest, CreateHierarchy)
{
  // Use built-in hierarchy creation
  auto hierarchy = CreateParentChildHierarchy("Parent", "Child");

  // Validate parent-child relationship
  ExpectNodeParent(hierarchy.child, hierarchy.parent);
  ExpectSceneNodeCount(2);
}
```

### 3. Error Testing
```cpp
class MyErrorTest : public fixtures::ErrorTest {};

NOLINT_TEST_F(MyErrorTest, InvalidNodeHandling)
{
  auto invalid_node = CreateLazyInvalidationNode("Test");
  ExpectNodeLazyInvalidated(invalid_node);
  ExpectNodeNotInScene(invalid_node);
}
```

### 4. Traversal Testing
```cpp
NOLINT_TEST_F(MyTraversalTest, BasicDepthFirst)
{
  auto hierarchy = CreateStandardTraversalHierarchy();
  auto visitor = CreateTrackingVisitor();

  auto result = traversal_->TraverseDepthFirst(hierarchy.root, visitor);

  ExpectTraversalResult(result, 6, 0, true);
  ExpectVisitedNodes({"root", "A", "C", "D", "B", "E"});
}
```

### 5. Flags Testing
```cpp
NOLINT_TEST_F(MyFlagsTest, BasicFlagOperations)
{
  SetAndValidateFlag(TestFlag::kVisible, true);
  ExpectFlagEffectiveValue(flags_, TestFlag::kVisible, true);
  ExpectDirtyFlagCount(flags_, 1);

  ProcessAndValidateClean();
  ExpectDirtyFlagCount(flags_, 0);
}
```

## Available Helpers

### Node Creation
- `CreateNode(name)` - Basic node creation
- `CreateNode(name, flags)` - Node with custom flags
- `CreateChildNode(parent, name)` - Child node creation
- `CreateVisibleNode(name, visible=true)` - Node with visibility setting
- `CreateInvisibleNode(name)` - Invisible node
- `CreateStaticNode(name)` - Static node
- `CreateNodeWithPosition(name, position)` - Node with transform

### Validation Helpers
- `ExpectNodeValidWithName(node, name)` - Validate node and name
- `ExpectNodeLazyInvalidated(node)` - Validate lazy invalidation
- `ExpectNodeNotInScene(node)` - Validate node not in scene
- `ExpectHandlesUnique(n1, n2, n3)` - Validate unique handles
- `ExpectSceneEmpty()` - Validate empty scene
- `ExpectSceneNodeCount(count)` - Validate node count
- `ExpectNodeParent(child, parent)` - Validate parent relationship
- `ExpectNodeIsRoot(node)` - Validate root node
- `ExpectTransformValues(node, pos, scale)` - Validate transforms

### Common Setups
- `CreateParentChildHierarchy()` - Simple parent-child
- `CreateParentTwoChildrenHierarchy()` - Parent with 2 children
- `CreateThreeLevelHierarchy()` - Three-level hierarchy
- `CreateMixedVisibilityHierarchy()` - Mixed visibility hierarchy

### Transform Helpers
- `SetNodePosition(node, position)` - Set node position
- `SetNodeScale(node, scale)` - Set node scale
- `SetTransformValues(node, pos, scale)` - Set position and scale
- `UpdateSingleNodeTransforms(node)` - Clear dirty flags

### Testing Utilities
- `ValidateUniqueHandles()` - Test handle uniqueness
- `TestSpecialCharacterNames()` - Test special/unicode names

## Migration Guide

To migrate existing tests to use this infrastructure:

1. **Replace fixture inheritance**:
   ```cpp
   // Old
   class MyTest : public ::testing::Test {
     std::shared_ptr<Scene> scene_;
     void SetUp() override { scene_ = std::make_shared<Scene>("Test", 1024); }
   };

   // New
   class MyTest : public oxygen::scene::testing::fixtures::BasicTest {
     // scene_ already available, no SetUp needed
   };
   ```

2. **Replace custom helpers with built-ins**:
   ```cpp
   // Old
   auto node = scene_->CreateNode("Test");
   EXPECT_TRUE(node.IsValid());
   auto obj = node.GetObject();
   EXPECT_TRUE(obj.has_value());
   EXPECT_EQ(obj->get().GetName(), "Test");

   // New
   auto node = CreateNode("Test");
   ExpectNodeValidWithName(node, "Test");
   ```

3. **Use hierarchy helpers**:
   ```cpp
   // Old
   auto parent = scene_->CreateNode("Parent");
   auto child_opt = scene_->CreateChildNode(parent, "Child");
   EXPECT_TRUE(child_opt.has_value());
   // ... validation code ...

   // New
   auto hierarchy = CreateParentChildHierarchy("Parent", "Child");
   ExpectNodeParent(hierarchy.child, hierarchy.parent);
   ```

## Benefits

1. **Reduced Duplication**: Eliminates 50%+ code duplication across test files
2. **Consistency**: Standardized testing patterns and validation approaches
3. **Maintainability**: Changes to testing patterns propagate automatically
4. **Readability**: Tests focus on behavior rather than setup boilerplate
5. **Reliability**: Well-tested helper functions reduce test bugs
6. **Extensibility**: Easy to add new common patterns as needs arise

## Example Files

See `ExampleUsage_test.cpp` for comprehensive examples of using all fixtures and helpers.
