# SceneQuery Documentation

## Table of Contents

### [1. Overview](#1-overview)
- [1.1 Purpose and Scope](#11-purpose-and-scope)
- [1.2 Key Features at a Glance](#12-key-features-at-a-glance)
- [1.3 Integration with Scene System](#13-integration-with-scene-system)

### [2. Getting Started](#2-getting-started)
- [2.1 Basic Usage Examples](#21-basic-usage-examples)
- [2.2 Entry Point: Scene::Query()](#22-entry-point-scenequery)
- [2.3 Common Query Patterns](#23-common-query-patterns)

### [3. Core Query Operations](#3-core-query-operations)
- [3.1 FindFirst - Single Node Queries](#31-findfirst---single-node-queries)
- [3.2 Collect - Multi-Node Queries](#32-collect---multi-node-queries)
- [3.3 Count - Counting Operations](#33-count---counting-operations)
- [3.4 Any - Existence Checks](#34-any---existence-checks)
- [3.5 Query Results and Error Handling](#35-query-results-and-error-handling)

### [4. Path-Based Navigation](#4-path-based-navigation)
- [4.1 Path Syntax and Patterns](#41-path-syntax-and-patterns)
- [4.2 Absolute vs Relative Paths](#42-absolute-vs-relative-paths)
- [4.3 Wildcard Support (* and **)](#43-wildcard-support--and-)
- [4.4 Path Query Performance](#44-path-query-performance)

### [5. Batch Query System](#5-batch-query-system)
- [5.1 Batch Execution Concept](#51-batch-execution-concept)
- [5.2 When to Use Batch Queries](#52-when-to-use-batch-queries)
- [5.3 Batch API Reference](#53-batch-api-reference)
- [5.4 Performance Benefits](#54-performance-benefits)
- [5.5 Limitations and Constraints](#55-limitations-and-constraints)

### [6. Query Scope Configuration](#6-query-scope-configuration)
- [6.1 Understanding Traversal Scope](#61-understanding-traversal-scope)
- [6.2 Scope Management API](#62-scope-management-api)
- [6.3 Full Scene vs Scoped Queries](#63-full-scene-vs-scoped-queries)
- [6.4 Scope Impact on All Operations](#64-scope-impact-on-all-operations)

### [7. Performance Characteristics](#7-performance-characteristics)
- [7.1 Time Complexity Analysis](#71-time-complexity-analysis)
- [7.2 Memory Usage Patterns](#72-memory-usage-patterns)
- [7.3 Cache Optimization](#73-cache-optimization)
- [7.4 Early Termination Strategies](#74-early-termination-strategies)

### [8. Advanced Topics](#8-advanced-topics)
- [8.1 Custom Predicates](#81-custom-predicates)
- [8.2 Container Type Requirements](#82-container-type-requirements)
- [8.3 Const Correctness Guarantees](#83-const-correctness-guarantees)
- [8.4 Thread Safety Considerations](#84-thread-safety-considerations)

### [9. Architecture Deep Dive](#9-architecture-deep-dive)
- [9.1 Type Erasure Pattern](#91-type-erasure-pattern)
- [9.2 Reference-Based Output Design](#92-reference-based-output-design)
- [9.3 Coroutine-Based Batch Execution](#93-coroutine-based-batch-execution)
- [9.4 SceneTraversal Integration](#94-scenetraversal-integration)

### [10. API Reference](#10-api-reference)
- [10.1 SceneQuery Class Interface](#101-scenequery-class-interface)
- [10.2 Result Types (QueryResult, BatchResult)](#102-result-types-queryresult-batchresult)
- [10.3 Template Parameters and Concepts](#103-template-parameters-and-concepts)
- [10.4 Error Codes and Diagnostics](#104-error-codes-and-diagnostics)

### [11. Best Practices](#11-best-practices)
- [11.1 Performance Guidelines](#111-performance-guidelines)
- [11.2 Memory Management](#112-memory-management)
- [11.3 Error Handling Patterns](#113-error-handling-patterns)
- [11.4 Common Pitfalls](#114-common-pitfalls)

### [12. Implementation Notes](#12-implementation-notes)
- [12.1 Design Decisions and Rationale](#121-design-decisions-and-rationale)
- [12.2 Alternative Approaches Considered](#122-alternative-approaches-considered)
- [12.3 Future Enhancement Opportunities](#123-future-enhancement-opportunities)

### [13. Testing and Validation](#13-testing-and-validation)
- [13.1 Unit Test Coverage](#131-unit-test-coverage)
- [13.2 Performance Benchmarks](#132-performance-benchmarks)
- [13.3 Edge Cases and Error Conditions](#133-edge-cases-and-error-conditions)

---

## 1. Overview

### 1.1 Purpose and Scope

The SceneQuery system provides a high-performance query interface for the Oxygen Engine scene graph, designed to address the core needs of game development workflows. It enables efficient searching, filtering, and collection of scene nodes through predicate-based queries and path-based navigation.

The system is built on the established `SceneTraversal` infrastructure and follows the same architectural patterns as `Scene::Traverse()`, providing a familiar and consistent API for developers.

### 1.2 Key Features at a Glance

**✅ Complete Implementation Status:**

- **Core Query Operations**: FindFirst, Collect, Count, Any with const-correct `ConstVisitedNode` access
- **Batch Execution Framework**: Single-traversal execution of multiple queries via coroutine coordination
- **Path-Based Navigation**: Absolute/relative paths with wildcard support (`*` and `**`)
- **Type-Erased Implementation**: Complex logic hidden in .cpp files for reduced header complexity
- **Reference-Based Output**: User-controlled memory allocation with zero-copy semantics
- **Scene Lifetime Safety**: `weak_ptr` validation prevents dangling references
- **Dual Execution Modes**: Automatic routing between immediate and batch execution

### 1.3 Integration with Scene System

SceneQuery integrates seamlessly with the existing Scene system through the `Scene::Query()` entry point, following established engine patterns:

```cpp
// Entry point following Scene::Traverse() pattern
auto scene = std::make_shared<Scene>("GameWorld");
auto query = scene->Query(); // Returns SceneQuery instance
```

## 2. Getting Started

### 2.1 Basic Usage Examples

```cpp
// Basic integration - get query interface from scene
auto scene = std::make_shared<Scene>("GameWorld");
auto query = scene->Query();

// Single query operations with reference-based output
std::optional<SceneNode> player;
auto result1 = query.FindFirst(player, [](const auto& visited) {
  return visited.node_impl->GetName() == "Player";
});

std::vector<SceneNode> enemies;
auto result2 = query.Collect(enemies, [](const auto& visited) {
  return visited.node_impl->GetName().starts_with("Enemy");
});
```

### 2.2 Entry Point: Scene::Query()

The `Scene::Query()` method is the single entry point for all query operations, returning a `SceneQuery` instance configured for the specific scene:

```cpp
auto Query() const -> SceneQuery;
```

**Key Design**: Following the established pattern where `Scene::Traverse()` provides access to `SceneTraversal`, the query system provides a single entry point for targeted node searches.

### 2.3 Common Query Patterns

**Immediate Mode Operations:**
```cpp
SceneQuery query(scene);

// FindFirst - early termination when target found
std::optional<SceneNode> player;
query.FindFirst(player, [](const auto& node) {
    return node.GetName() == "Player";
});

// Collect - user-controlled allocation
std::vector<SceneNode> enemies;
enemies.reserve(100); // Pre-allocate for performance
query.Collect(enemies, [](const auto& node) {
    return node.HasTag("enemy");
});

// Count and Any - no allocation required
std::optional<size_t> destructible_count;
query.Count(destructible_count, [](const auto& node) {
    return node.HasTag("destructible");
});

std::optional<bool> has_explosions;
query.Any(has_explosions, [](const auto& node) {
    return node.HasTag("explosion");
});
```

## 3. Core Query Operations

### 3.1 FindFirst - Single Node Queries

**Purpose**: Find the first node matching a predicate with early termination optimization.

```cpp
template <std::predicate<const ConstVisitedNode&> Predicate>
auto FindFirst(std::optional<SceneNode>& output, Predicate&& predicate) const -> QueryResult;
```

**Key Characteristics**:
- **Early Termination**: Stops traversal immediately when first match is found
- **Reference-Based Output**: Populates `std::optional<SceneNode>&` parameter
- **Performance**: O(1) to O(n) with typical sub-millisecond response times

### 3.2 Collect - Multi-Node Queries

**Purpose**: Collect all nodes matching a predicate into a user-provided container.

```cpp
template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
auto Collect(Container& container, Predicate&& predicate) const noexcept -> QueryResult;
```

**Key Characteristics**:
- **User-Controlled Memory**: Eliminates forced allocations, enables custom allocators
- **Container Flexibility**: Supports any container with `emplace_back`-compatible interface
- **Performance**: O(n) with optimal cache locality

### 3.3 Count - Counting Operations

**Purpose**: Count nodes matching a predicate without allocation overhead.

```cpp
template <std::predicate<const ConstVisitedNode&> Predicate>
auto Count(std::optional<size_t>& output, Predicate&& predicate) const noexcept -> QueryResult;
```

**Key Characteristics**:
- **Zero Allocation**: No memory allocation required
- **Reference Output**: Populates `std::optional<size_t>&` parameter
- **Performance**: O(n) traversal with counting only

### 3.4 Any - Existence Checks

**Purpose**: Check if any node matches a predicate with early termination.

```cpp
template <std::predicate<const ConstVisitedNode&> Predicate>
auto Any(std::optional<bool>& output, Predicate&& predicate) const noexcept -> QueryResult;
```

**Key Characteristics**:
- **Early Termination**: Stops immediately when first match is found
- **Boolean Result**: Simple existence check
- **Performance**: O(1) to O(n) with aggressive optimization

### 3.5 Query Results and Error Handling

**QueryResult Structure**:
```cpp
struct QueryResult {
  std::size_t nodes_examined = 0;
  std::size_t nodes_matched = 0;
  std::optional<std::string> error_message {};
  explicit operator bool() const noexcept { return !error_message.has_value(); }
};
```

**Key Features**:
- **Performance Metrics**: Tracks nodes examined and matched for optimization insights
- **Error Handling**: Optional error message for failure cases
- **Boolean Conversion**: Easy success/failure checking

## 4. Path-Based Navigation

### 4.1 Path Syntax and Patterns

**Path Navigation from Current Query Scope**:
```cpp
// Configure query scope first
auto query = scene->Query();
query.AddToTraversalScope(world_node);  // Limit traversal to world_node hierarchy

std::optional<SceneNode> player;
query.FindFirstByPath(player, "Player");  // Find "Player" starting from world_node

std::optional<SceneNode> weapon;
query.FindFirstByPath(weapon, "Player/Equipment/Weapon");  // Multi-level path from scope roots
```

**Root-Level Paths** (starting with "/"):
```cpp
std::optional<SceneNode> foo_node;
query.FindFirstByPath(foo_node, "/Foo");  // From any traversal scope root with empty name "", find child "Foo"
```

**Full Scene Traversal** (default scope):
```cpp
auto query = scene->Query();  // Default: traverses entire scene
// or explicitly reset scope
query.ResetTraversalScope();  // Clear any scope restrictions

std::optional<SceneNode> any_player;
query.FindFirstByPath(any_player, "World/Player");  // Search from all scene roots
```

### 4.2 Path Syntax Details

**Path Separators and Structure**:
- Uses `/` as hierarchical separator
- Paths are relative to current traversal scope (see Section 5 for scope configuration)
- Simple paths without wildcards use direct navigation for O(depth) performance

**Root-Level Paths** (starting with "/"):
```cpp
std::optional<SceneNode> foo_node;
query.FindFirstByPath(foo_node, "/Foo");  // From traversal scope roots with empty name "", find child "Foo"
```

**Multi-Level Navigation**:
```cpp
std::optional<SceneNode> weapon;
query.FindFirstByPath(weapon, "Player/Equipment/Weapon");  // Navigate through hierarchy
```

### 4.3 Wildcard Support (* and **)

**Single-Level Wildcards (`*`)**:
```cpp
std::vector<SceneNode> all_enemies;
query.CollectByPath(all_enemies, "*/Enemy");        // Direct children named Enemy
query.CollectByPath(all_enemies, "Level/*/Enemy");  // Enemy under Level's direct children
```

**Recursive Wildcards (`**`)**:
```cpp
std::vector<SceneNode> all_weapons;
query.CollectByPath(all_weapons, "**/Weapon");       // All weapons at any depth
query.CollectByPath(all_weapons, "Player/**/Weapon"); // All weapons under Player hierarchy
```

### 4.4 Path Query Performance

**Performance Characteristics**:
- **Simple Paths**: O(depth) complexity using direct navigation
- **Wildcard Patterns**: O(n) complexity using filtered traversal
- **Direct Navigation Optimization**: Bypasses full traversal for non-wildcard paths

**Path Parsing Architecture** (internal implementation):
```cpp
struct ParsedPath {
  std::vector<PathSegment> segments;
  bool is_valid = false;
  bool has_wildcards = false;
};

struct PathSegment {
  std::string name;
  bool is_wildcard_single = false;   // "*"
  bool is_wildcard_recursive = false; // "**"
  bool is_absolute = false;
};
```

## 5. Batch Query System

### 5.1 Batch Execution Concept

The batch execution system allows multiple queries to execute in a single SceneTraversal pass, providing significant performance benefits:

```cpp
// Key benefit: N queries → 1 traversal instead of N traversals
std::optional<SceneNode> player;
std::vector<SceneNode> enemies;
std::optional<size_t> count;
std::optional<bool> found;

auto result = query.ExecuteBatch([&](auto& q) {
    q.BatchFindFirst(player, player_predicate);         // Operation 1 (void return)
    q.BatchCollect(enemies, enemy_predicate);           // Operation 2 (void return)
    q.BatchCount(count, pickup_predicate);              // Operation 3 (void return)
    q.BatchAny(found, explosion_predicate);             // Operation 4 (void return)
}); // Single traversal executes all 4 operations
```

### 5.2 When to Use Batch Queries

**Use ExecuteBatch when:**
- Running multiple queries in the same frame (game systems)
- Performance is critical (60+ FPS requirements)
- Queries have overlapping search spaces
- Cache locality is important

**Use individual queries when:**
- Single query needed
- Conditional logic between queries
- Early termination based on first query result
- Debugging or development scenarios
- **Path-based navigation is required** (FindFirstByPath, CollectByPath)

### 5.3 Batch API Reference

**Batch Execution Framework**:
```cpp
template <typename BatchFunc>
auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;
```

**Batch-Specific Methods (Return `void`)**:
```cpp
template <std::predicate<const ConstVisitedNode&> Predicate>
auto BatchFindFirst(std::optional<SceneNode>& output, Predicate&& pred) const noexcept; // -> void

template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
auto BatchCollect(Container& result, Predicate&& pred) const noexcept -> void;

template <std::predicate<const ConstVisitedNode&> Predicate>
auto BatchCount(std::optional<size_t>& output, Predicate&& pred) const noexcept; // -> void

template <std::predicate<const ConstVisitedNode&> Predicate>
auto BatchAny(std::optional<bool>& output, Predicate&& pred) const noexcept; // -> void
```

### 5.4 Performance Benefits

**Key Batch Execution Benefits**:
1. **Performance Multiplication**: N queries execute in 1 traversal instead of N traversals
2. **Coroutine Coordination**: Each operation runs as separate coroutine with BroadcastChannel distribution
3. **Smart Termination**: Stops when all FindFirst/Any operations complete via coroutine combinators
4. **Cache Efficiency**: Single pass maximizes cache locality
5. **Zero Overhead**: When not in batch mode, behaves exactly like immediate queries
6. **Type Safety**: Lambda capture maintains full type safety
7. **Memory Control**: User manages all container allocations upfront

**Performance Characteristics**:
- **Time Complexity**: O(n) for n nodes regardless of number of queries in batch
- **Memory**: User-controlled allocation for all result containers
- **Cache Performance**: Single traversal maximizes cache hit rates
- **Scalability**: Performance improvement scales linearly with number of queries

### 5.5 Limitations and Constraints

**Critical Architectural Constraint**: Path-based queries (`FindFirstByPath`, `CollectByPath`) are **intentionally excluded** from batch execution by design.

**Rationale**:
- Path queries use optimized direct navigation (O(depth) complexity)
- Batch operations require full traversal (O(n) complexity)
- Mixing navigation patterns would eliminate performance benefits
- Implementation complexity would significantly increase without proportional benefit

**Impact**:
- Path queries must be executed individually
- Batch operations limited to predicate-based queries only
- Clear separation of concerns between navigation and traversal patterns

## 6. Query Scope Configuration

### 6.1 Understanding Traversal Scope

The SceneQuery system uses a powerful traversal scope mechanism that determines which nodes serve as starting points for **ALL** query operations. This is implemented through the `traversal_scope_` vector of SceneNode objects and affects every type of query - predicate-based, path-based, and batch operations.

**Scope Concept**:
- **Empty Scope (default)**: Queries traverse the entire scene starting from all scene root nodes
- **Configured Scope**: Queries are limited to specific hierarchies defined by `AddToTraversalScope()`
- **Scope Persistence**: Once configured, scope affects all subsequent operations until explicitly changed
- **Universal Impact**: Scope applies to FindFirst, Collect, Count, Any, path queries, and batch operations

### 6.2 Scope Management API

**Available Scope Control Methods**:
```cpp
class SceneQuery {
public:
  // Reset to full scene traversal
  auto ResetTraversalScope() noexcept -> SceneQuery&;

  // Add single hierarchy to scope
  auto AddToTraversalScope(const SceneNode& starting_node) noexcept -> SceneQuery&;

  // Add multiple hierarchies to scope
  auto AddToTraversalScope(std::span<const SceneNode> starting_nodes) noexcept -> SceneQuery&;
};
```

**Method Chaining Support**:
```cpp
auto query = scene->Query();

// Fluent interface for scope configuration
query.ResetTraversalScope()
     .AddToTraversalScope(player_node)
     .AddToTraversalScope(ui_root_node);

// Now all queries are scoped to player and UI hierarchies
```

### 6.3 Full Scene vs Scoped Queries

**Default Behavior (Empty Scope)**:
```cpp
auto query = scene->Query();  // Default: empty traversal scope

// All operations search the entire scene
std::optional<SceneNode> any_player;
query.FindFirst(any_player, player_predicate);  // Searches ALL scene roots

std::vector<SceneNode> all_enemies;
query.Collect(all_enemies, enemy_predicate);    // Searches entire scene graph

std::optional<SceneNode> any_weapon;
query.FindFirstByPath(any_weapon, "World/Player/Weapon");  // Searches from all scene roots
```

**Scoped Queries (Configured Scope)**:
```cpp
auto query = scene->Query();

// Configure scope to specific hierarchies
query.AddToTraversalScope(level1_root)
     .AddToTraversalScope(level2_root);

// Now ALL operations are limited to these hierarchies
std::optional<SceneNode> scoped_player;
query.FindFirst(scoped_player, player_predicate);  // Only searches level1 and level2

std::vector<SceneNode> scoped_enemies;
query.Collect(scoped_enemies, enemy_predicate);    // Only in specified hierarchies

std::optional<SceneNode> scoped_weapon;
query.FindFirstByPath(scoped_weapon, "Player/Weapon");  // Only from level1_root and level2_root
```

**Dynamic Scope Changes**:
```cpp
auto query = scene->Query();

// Start with player hierarchy
query.AddToTraversalScope(player_node);
auto player_items = query.Collect(items, item_predicate);  // Only player items

// Switch to UI hierarchy
query.ResetTraversalScope()
     .AddToTraversalScope(ui_root);
auto ui_elements = query.Collect(elements, ui_predicate);  // Only UI elements

// Back to full scene
query.ResetTraversalScope();
auto all_objects = query.Collect(objects, any_predicate);  // Entire scene again
```

### 6.4 Scope Impact on All Operations

**Predicate-Based Queries with Scope**:
```cpp
auto query = scene->Query();
query.AddToTraversalScope(combat_zone);

// All predicate operations respect scope
std::optional<SceneNode> nearest_enemy;
query.FindFirst(nearest_enemy, [](const auto& visited) {
    return visited.node_impl->HasTag("enemy");
});  // Only finds enemies in combat_zone hierarchy

std::optional<size_t> weapon_count;
query.Count(weapon_count, weapon_predicate);  // Only counts weapons in combat_zone

std::optional<bool> has_explosions;
query.Any(has_explosions, explosion_predicate);  // Only checks combat_zone for explosions
```

**Path Queries with Scope**:
```cpp
auto query = scene->Query();
query.AddToTraversalScope(world_node);  // Limit to world hierarchy

// Path navigation starts from world_node
std::optional<SceneNode> player;
query.FindFirstByPath(player, "Player");  // Finds Player under world_node

std::vector<SceneNode> all_weapons;
query.CollectByPath(all_weapons, "**/Weapon");  // All weapons under world_node only
```

**Batch Operations with Scope**:
```cpp
auto query = scene->Query();
query.AddToTraversalScope(active_level);  // Scope to active level

// Batch operations respect scope - single traversal of active_level only
std::optional<SceneNode> player;
std::vector<SceneNode> enemies;
std::optional<size_t> pickup_count;

auto batch_result = query.ExecuteBatch([&](auto& q) {
    q.BatchFindFirst(player, player_predicate);      // Only in active_level
    q.BatchCollect(enemies, enemy_predicate);        // Only in active_level
    q.BatchCount(pickup_count, pickup_predicate);    // Only in active_level
});  // Single traversal of active_level hierarchy
```

**Implementation Details**:
- **Empty `traversal_scope_`**: Uses `traversal_.Traverse(visitor, order, filter)` for full scene
- **Non-empty `traversal_scope_`**: Uses `traversal_.TraverseHierarchies(traversal_scope_, visitor, order, filter)` for scoped traversal
- **Scope Validation**: All nodes in scope must belong to the same scene
- **Performance**: Scoped queries can be significantly faster by eliminating irrelevant subtrees
- **Memory**: Scope configuration has minimal memory overhead (vector of SceneNode handles)

## 7. Performance Characteristics

### 7.1 Time Complexity Analysis

**Immediate Mode Operations**:
- **FindFirst**: O(1) to O(n) with early termination, typically sub-millisecond
- **Collect**: O(n) with user-controlled allocation and optimal cache locality
- **Count/Any**: O(n) worst case, with early termination for Any queries
- **Path Navigation**: O(depth) for simple paths, O(n) for wildcard patterns

**Batch Mode Operations**:
- **Multi-Query Batches**: Single O(n) traversal regardless of query count
- **Coroutine Overhead**: Minimal suspension/resumption cost during traversal
- **Memory Efficiency**: Zero additional allocations beyond user-provided containers
- **Cache Performance**: Optimal locality through single-pass execution

### 7.2 Memory Usage Patterns

**User-Controlled Memory Allocation**:
```cpp
template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
auto Collect(Container& container, Predicate&& predicate) const noexcept -> QueryResult;
```

**Benefits**:
- Users provide containers, eliminating forced allocations
- Enables custom allocators and object pools
- Pre-sizing containers for known upper bounds
- Integration with frame-based memory schemes

### 7.3 Cache Optimization

**Single Traversal Benefits**:
- Batch operations maximize cache locality through single-pass execution
- Early termination reduces unnecessary memory access
- SceneTraversal infrastructure optimized for cache-friendly access patterns

### 7.4 Early Termination Strategies

**Optimization Techniques**:
- `VisitResult::kStop` for immediate termination in FindFirst/Any operations
- Coroutine combinators (`AnyOf`/`AllOf`) for batch early termination
- Smart pruning and subtree rejection

## 8. Advanced Topics

### 8.1 Custom Predicates

**Predicate Requirements**:
```cpp
// Type-safe const-correct visitor/filter concepts
std::predicate<const ConstVisitedNode&> Predicate
```

**Implementation**:
- Compile-time const correctness validation via SceneTraversal concepts
- Automatic visitor type deduction (`ConstVisitedNode` vs `MutableVisitedNode`)
- Template concepts enforce correct parameter types at compile time

### 8.2 Container Type Requirements

**Supported Container Types**:
- Any container with `emplace_back`-compatible interface
- Custom allocator support through user-provided containers
- STL containers: `std::vector`, `std::deque`, `std::list`

### 8.3 Const Correctness Guarantees

**Complete Compile-Time Immutability**:
- Uses `ConstVisitedNode` for read-only scene graph access
- `SceneTraversal<const Scene>` enforces compile-time immutability
- Automatic const deduction based on Scene type via template specialization
- All SceneQuery operations guaranteed read-only

### 8.4 Thread Safety Considerations

**Scene Lifetime Safety**:
- `weak_ptr` validation prevents dangling references
- Consistent `weak_ptr` expiry checking with const-safe validation
- Robust error handling for edge cases like objects being destroyed mid-query

## 9. Architecture Deep Dive

### 9.1 Type Erasure Pattern

**Critical Architecture Note**: This is **NOT** a PIMPL pattern - it's a **Type Erasure Pattern** using `std::function`:

```cpp
// Key type alias for type erasure
using QueryPredicate = std::function<bool(const ConstVisitedNode&)>;

// Header: Template wrapper with type erasure
template <std::predicate<const ConstVisitedNode&> Predicate>
auto FindFirst(std::optional<SceneNode>& output, Predicate&& predicate) const -> QueryResult {
  // Type erasure: template parameter → std::function
  return FindFirstImpl(output, QueryPredicate(std::forward<Predicate>(predicate)));
}

// .cpp: Type-erased implementation
auto FindFirstImpl(std::optional<SceneNode>& output,
  const QueryPredicate& predicate) const noexcept -> QueryResult {
  // Complex implementation logic hidden from header
}
```

**Applied Type Erasure Mechanisms**:
1. **Predicate Type Erasure**: `template<Predicate> → QueryPredicate (std::function)`
2. **Container Type Erasure**: `template<Container> → std::function<void(const SceneNode&)>`
3. **Batch Function Type Erasure**: `template<BatchFunc> → std::function<void(const SceneQuery&)>`

### 9.2 Reference-Based Output Design

**Critical Design Decision**: ALL methods take output by reference, not return values.

**Architecture**:
- **Immediate Mode**: `FindFirst(std::optional<SceneNode>& output, predicate) -> QueryResult`
- **Batch Mode**: `BatchFindFirst(std::optional<SceneNode>& output, predicate) -> void`
- **Rationale**: Enables direct population during single traversal, consistent API across modes
- **Performance**: Eliminates return value overhead and copying

### 9.3 Coroutine-Based Batch Execution

**Batch Execution Architecture**:
- **BroadcastChannel Architecture**: Uses `oxygen::co::BroadcastChannel<ConstVisitedNode>` for node distribution
- **Coroutine Coordination**: Each operation runs as separate coroutine via `BatchQueryExecutor`
- **MinimalEventLoop**: Custom event loop implementation for coroutine execution
- **AnyOf/AllOf Combinators**: Complex coordination patterns for early termination
- **Single Traversal**: Streams nodes to all operations simultaneously

**Batch Operation Storage**:
```cpp
struct BatchOperation {
  std::function<bool(const ConstVisitedNode&)> predicate;
  enum class Status { Pending, Completed, Failed } status = Status::Pending;
  QueryResult result;
  // Internal coordination fields for coroutine-based execution
  std::optional<SceneNode> internal_found_node;
  std::optional<bool> internal_any_result;
  std::size_t internal_count_result = 0;
  std::function<oxygen::co::Co<>(oxygen::co::BroadcastChannel<ConstVisitedNode>&)> operation;
};
```

### 9.4 SceneTraversal Integration

**Dual Traversal Systems**:
- **Immediate Mode**: `SceneTraversal<const Scene>` for direct, synchronous queries
- **Batch Mode**: `AsyncSceneTraversal<const Scene>` for coroutine-based execution
- **Routing**: `batch_active_` flag with `EnsureCanExecute()` validation
- **Separation**: Clean architectural boundaries between execution modes

**Integration Architecture**:
```cpp
class SceneQuery {
private:
  std::weak_ptr<const Scene> scene_weak_;
  SceneTraversal<const Scene> traversal_;
  AsyncSceneTraversal<const Scene> async_traversal_;

  // Batch execution state
  mutable bool batch_active_ = false;
  mutable void* batch_coordinator_ = nullptr; // BatchQueryExecutor*
};
```

## 10. API Reference

### 10.1 SceneQuery Class Interface

```cpp
class SceneQuery {
public:
  //! Type erased (std::function) predicate function type, used to select nodes
  //! for a query, during a traversal.
  using QueryPredicate = std::function<bool(const ConstVisitedNode&)>;

  // Core search operations implemented with reference-based output
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto FindFirst(std::optional<SceneNode>& output, Predicate&& pred) const -> QueryResult;

  template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
  auto Collect(Container& container, Predicate&& pred) const noexcept -> QueryResult;

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Count(std::optional<size_t>& output, Predicate&& pred) const noexcept -> QueryResult;

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Any(std::optional<bool>& output, Predicate&& pred) const noexcept -> QueryResult;

  // Path-based queries (core functionality for game engine workflows)
  auto FindFirstByPath(std::optional<SceneNode>& output, std::string_view path) const noexcept -> QueryResult;

  template <typename Container>
  auto CollectByPath(Container& container, std::string_view path_pattern) const noexcept -> QueryResult;

  // Batch execution - execute multiple queries in single traversal pass
  template <typename BatchFunc>
  auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;

  // Batch-specific methods (return void, populate by reference)
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto BatchFindFirst(std::optional<SceneNode>& output, Predicate&& pred) const noexcept; // -> void

  template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
  auto BatchCollect(Container& result, Predicate&& pred) const noexcept -> void;

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto BatchCount(std::optional<size_t>& output, Predicate&& pred) const noexcept; // -> void

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto BatchAny(std::optional<bool>& output, Predicate&& pred) const noexcept; // -> void
};
```

### 10.2 Result Types (QueryResult, BatchResult)

**QueryResult Structure**:
```cpp
struct QueryResult {
  std::size_t nodes_examined = 0;
  std::size_t nodes_matched = 0;
  std::optional<std::string> error_message {};
  explicit operator bool() const noexcept { return !error_message.has_value(); }
};
```

**BatchResult Structure**:
```cpp
struct BatchResult {
  std::size_t nodes_examined = 0;
  std::size_t total_matches = 0;
  bool success = true;
  std::vector<QueryResult> operation_results;
  explicit operator bool() const noexcept { return success; }
};
```

**Implementation Notes**:
- **Reference-Based Output**: All actual results passed via reference parameters
- **Metrics in Return Values**: `QueryResult`/`BatchResult` contain only performance data
- **Immediate Mode**: Returns `QueryResult` with metrics, populates output by reference
- **Batch Mode**: Returns `void`, metrics aggregated in final `BatchResult`

### 10.3 Template Parameters and Concepts

**Predicate Concept**:
```cpp
std::predicate<const ConstVisitedNode&> Predicate
```

**Container Requirements**:
- Must support `emplace_back`-compatible interface
- Compatible with `std::back_inserter`
- Custom allocator support through user control

### 10.4 Error Codes and Diagnostics

**Error Handling Strategy**:
- `weak_ptr` validation for scene lifetime safety
- Optional error messages in `QueryResult`
- `noexcept` design for performance-critical paths
- Boolean conversion operators for easy success/failure checking

## 11. Best Practices

### 11.1 Performance Guidelines

**Optimization Strategies**:
1. **Use Batch Queries**: For multiple queries in the same frame
2. **Pre-allocate Containers**: Use `reserve()` for known upper bounds
3. **Early Termination**: Prefer `FindFirst` and `Any` when only existence matters
4. **Path Optimization**: Use simple paths without wildcards when possible
5. **Cache Locality**: Group related queries in batch operations

### 11.2 Memory Management

**Memory Best Practices**:
- **User-Controlled Allocation**: Always provide containers to eliminate forced allocations
- **Custom Allocators**: Integrate with object pools and frame-based memory schemes
- **Container Pre-sizing**: Use `reserve()` for performance-critical scenarios
- **RAII Pattern**: Leverage automatic lifetime management through reference counting

### 11.3 Error Handling Patterns

**Recommended Error Handling**:
```cpp
std::optional<SceneNode> result;
auto query_result = query.FindFirst(result, predicate);

if (query_result) {
    // Success - check if node was found
    if (result.has_value()) {
        // Process found node
    }
} else {
    // Handle error
    std::cout << "Query failed: " << query_result.error_message.value_or("Unknown error") << std::endl;
}
```

### 11.4 Common Pitfalls

**Avoid These Patterns**:
1. **Multiple Individual Queries**: Use batch execution for better performance
2. **Ignoring Container Pre-allocation**: Can cause performance degradation
3. **Path Queries in Batches**: Architecturally incompatible, will fail
4. **Scene Lifetime Issues**: Always ensure scene remains valid during queries

## 12. Implementation Notes

### 12.1 Design Decisions and Rationale

**Key Architectural Decisions**:

1. **Type Erasure Architecture**: Uses `std::function` rather than template-based implementations
   - Eliminates template bloat and provides stable ABI
   - Enables runtime composition with negligible overhead (~2-5 CPU cycles)

2. **Reference-Based Output Pattern**: Output parameters instead of return values
   - Eliminates copy/move operations and gives complete memory control
   - Maintains consistent API across immediate and batch modes

3. **Coroutine-Based Batch Execution**: Cooperative multitasking without thread overhead
   - Enables interleaved execution during single traversal pass
   - Clean separation between traversal logic and query execution

### 12.2 Alternative Approaches Considered

**Option 1: Enhanced Direct API on Scene Class**
- **Pros**: Simple to use, direct access
- **Cons**: Clutters Scene API, always allocates vectors, not extensible

**Option 2: Builder Pattern Query System**
- **Pros**: Flexible and composable, type-safe
- **Cons**: Still forces allocation, more complex implementation

**Option 3: True std::ranges Integration**
- **Pros**: C++20 compatibility, composable with standard library
- **Cons**: Complex iterator infrastructure, performance overhead, allocation issues

**Selected Option 4: Single Entry Point with Performance Focus**
- **Pros**: Zero-copy performance, user-controlled allocation, follows established patterns
- **Cons**: Domain-specific interface requiring learning

### 12.3 Future Enhancement Opportunities

**Potential Improvements**:
- Spatial indexing integration for performance optimization
- Caching layers for frequently accessed queries
- Specialized data structures for specific query patterns
- Extended wildcard pattern support

## 13. Testing and Validation

### 13.1 Unit Test Coverage

**Test Implementation Status**: ✅ Complete
- All core query operations tested with comprehensive scenarios
- Batch execution framework validated with multi-operation tests
- Path parsing and wildcard matching verified
- Error handling and edge cases covered

### 13.2 Performance Benchmarks

**Performance Validation**:
- Sub-millisecond response times for typical game scenarios
- Batch execution showing linear performance improvement with query count
- Memory allocation patterns validated for zero-copy semantics
- Cache locality measurements confirming single-traversal benefits

### 13.3 Edge Cases and Error Conditions

**Validated Scenarios**:
- Scene destruction during query execution
- Invalid path patterns and malformed predicates
- Empty scenes and null reference handling
- Batch execution with mixed operation types
