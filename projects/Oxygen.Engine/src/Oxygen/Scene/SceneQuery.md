# SceneQuery Implementation Status & Design

## Current Implementation Status

### ✅ Implemented Features

| Feature | Status | Notes |
|---------|--------|-------|
| Core Query Construction | ✅ Complete | `SceneQuery(std::shared_ptr<const Scene>)` |
| Const-Correct Predicate-Based FindFirst | ✅ Complete | Uses `ConstVisitedNode` for read-only queries |
| Const-Correct Predicate-Based Collect | 🔄 Partial | Immediate mode complete, batch mode incomplete |
| Const-Correct Predicate-Based Count | ✅ Complete | Returns `QueryResult` with metrics |
| Const-Correct Predicate-Based Any | ✅ Complete | Early termination optimization |
| Batch Execution Framework | 🔄 Partial | Core framework complete, ExecuteBatchCollect missing |
| Query Result Types | ✅ Complete | `QueryResult` and `BatchResult` |
| Scene Lifetime Safety | ✅ Complete | `weak_ptr` validation |
| Dual Execution Modes | ✅ Complete | Automatic batch/immediate routing |
| Type-Erased Batch Operations | ✅ Complete | `BatchOperation` struct with result handlers |
| Const-Correct SceneTraversal Integration | ✅ Complete | `SceneTraversal<const Scene>` for read-only operations |
| Scene Integration | ✅ Complete | `Scene::Query()` entry point implemented |

### 🔄 Partially Implemented

| Feature | Status | Notes |
|---------|--------|-------|
| Const-Correct Predicate-Based Collect | 🔄 Partial | Immediate mode complete, batch mode incomplete |

### ❌ Not Yet Implemented

| Feature | Status | Reason |
|---------|--------|--------|
| Path-Based Queries | ❌ Commented Out | Complex path parsing/navigation logic |
| `ExecuteBatchCollect` | ❌ Empty Implementation | Batch collection logic incomplete |
| Path Wildcard Support | ❌ Missing | `ParsePath`, `CreatePathFilter` helpers |

### 🚫 Intentionally Excluded from Batch Operations

| Feature | Status | Design Rationale |
|---------|--------|------------------|
| Path-Based Queries in Batches | 🚫 By Design | Direct navigation incompatible with batched traversal |

## Key Design Choices Implemented

### 1. Const-Correct Dual Execution Architecture

The implementation uses a `batch_active_` flag to route calls with full const
correctness:

```cpp
template <std::predicate<const ConstVisitedNode&> Predicate>
auto FindFirst(Predicate&& predicate) const noexcept -> std::optional<SceneNode>;

template <typename BatchFunc>
auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;
```

**Implementation**:
- Uses `ConstVisitedNode` for read-only scene graph access
- `SceneTraversal<const Scene>` enforces compile-time immutability
- Automatic const deduction based on Scene type via template specialization

### 2. User-Controlled Memory Allocation

```cpp
template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
auto Collect(Container& container, Predicate&& predicate) const noexcept -> QueryResult;
```

**Implementation**: Users provide containers, eliminating forced allocations and
enabling custom allocators.

### 3. Type-Safe Const-Correct Visitor/Filter Concepts

**Implementation**:
- Compile-time const correctness validation via SceneTraversal concepts
- Automatic visitor type deduction (`ConstVisitedNode` vs `MutableVisitedNode`)
- Template concepts enforce correct parameter types at compile time

### 4. Type-Erased Batch Operations

```cpp
struct BatchOperation {
  std::function<bool(const ConstVisitedNode&)> predicate;
  enum class Type : uint8_t { kFindFirst, kCollect, kCount, kAny } type;
  void* result_destination; // Type-erased for flexibility
  std::function<void(const ConstVisitedNode&)> result_handler;
  bool has_terminated = false;
  mutable bool matched_current_node = false;
};
```

**Implementation**: Lambda captures maintain type safety while allowing storage
in a single container.

### 4. Performance-First Implementation Patterns

**Immediate Execution**:
- Direct `SceneTraversal` integration with custom filters
- `VisitResult::kStop` for early termination in FindFirst/Any
- Separate node examination counting in filters

**Batch Execution**:
- 4-phase execution: `BatchBegin()` → `CreateCompositeFilter()` → `Execute()` →
  `BatchEnd()`
- Composite filtering with per-operation match flags
- Single traversal for multiple queries

### 5. Result Types with Performance Metrics

```cpp
struct QueryResult {
  std::size_t nodes_examined = 0;
  std::size_t nodes_matched = 0;
  bool completed = true;
  explicit operator bool() const noexcept { return completed; }
};

struct BatchResult {
  std::size_t nodes_examined = 0;
  std::size_t total_matches = 0;
  bool completed = true;
  explicit operator bool() const noexcept { return completed; }
};
```

**Implementation**: Provide performance insights while maintaining lightweight
operation.

## Public API Summary

### Core Query Methods (✅ Implemented)

| Method | Return Type | Implementation Status |
|--------|-------------|----------------------|
| `FindFirst<Predicate>(pred)` | `std::optional<SceneNode>` | ✅ Complete - Both paths work |
| `Collect<Container, Predicate>(container, pred)` | `QueryResult` | ✅ Immediate complete, batch incomplete |
| `Count<Predicate>(pred)` | `QueryResult` | ✅ Complete - Both paths work |
| `Any<Predicate>(pred)` | `std::optional<bool>` | ✅ Complete - Both paths work |
| `ExecuteBatch<BatchFunc>(func)` | `BatchResult` | ✅ Framework complete, collect missing |

### Path-Based Methods (❌ Not Implemented)

| Method | Return Type | Status |
|--------|-------------|--------|
| `FindFirstByPath(path)` | `std::optional<SceneNode>` | ❌ Commented out |
| `FindFirstByPath(context, path)` | `std::optional<SceneNode>` | ❌ Commented out |
| `CollectByPath<Container>(container, pattern)` | `QueryResult` | ❌ Commented out |

## Implementation Analysis

### What Works Well

| Aspect | Implementation Quality | Notes |
|--------|----------------------|-------|
| Const Correctness | ✅ Excellent | Complete compile-time immutability via `ConstVisitedNode` |
| Scene Lifetime Management | ✅ Robust | `weak_ptr` prevents dangling references |
| Batch Framework | ✅ Solid | 4-phase execution is well-structured |
| Type Safety | ✅ Excellent | Template predicates with concepts and auto const deduction |
| Early Termination | ✅ Effective | `VisitResult::kStop` and `has_terminated` flags |
| Memory Control | ✅ Excellent | User-provided containers eliminate allocations |

### Current Issues

| Issue | Severity | Description |
|-------|----------|-------------|
| Incomplete Batch Collect | 🔴 High | `ExecuteBatchCollect` is empty |
| BatchResult Total Matches | 🟡 Medium | Calculation logic is simplified/incomplete |

### Architectural Strengths

1. **Const Correctness**: Complete compile-time immutability guarantees via
   `SceneTraversal<const Scene>`
2. **Clear Separation**: Batch vs immediate execution paths are well-separated
3. **Consistent Patterns**: All methods follow the same routing logic with type
   safety
4. **Performance Focus**: Single traversal for batches, early termination with
   const-correct visitors
5. **Type Safety**: Template predicates with concept constraints and automatic
   const deduction
6. **Error Handling**: Consistent `weak_ptr` expiry checking with const-safe
   validation
7. **Seamless Integration**: `Scene::Query()` provides intuitive access following
   established engine patterns like `Scene::Traverse()`

## Objectives

The primary goal is to design a high-performance query system for the Oxygen
Engine scene graph that addresses the core needs of game development workflows
while maintaining consistency with established engine patterns.

### Game Engine Query Requirements

1. **Performance-Critical Operations**: Game engines need sub-millisecond query
   response times for runtime gameplay systems like AI pathfinding, physics
   collision detection, and rendering culling that execute every frame.

2. **Memory-Conscious Design**: Game development operates under strict memory
   budgets where unnecessary allocations can cause frame drops, garbage
   collection pauses, or memory fragmentation affecting performance.

3. **Spatial and Hierarchical Queries**: Game objects are often queried by
   spatial relationships (nearby enemies, objects in view), hierarchical
   relationships (all children of a player), or gameplay semantics (all
   interactive objects, all weapons).

4. **Frame-Rate Consistency**: Query performance must be predictable and
   consistent to maintain stable frame rates, avoiding expensive operations that
   could cause frame time spikes.

5. **Runtime Flexibility**: Game logic requires dynamic queries based on
   gameplay state - finding targets within range, locating specific object
   types, or filtering by runtime properties like visibility or player
   ownership.

6. **Batch Processing Support**: Many game systems benefit from processing
   multiple related queries together (updating all enemy AI, rendering all
   visible objects) for cache efficiency and reduced overhead.

7. **Early Exit Optimization**: Gameplay often needs to find "any" match (is
   there a player nearby?) or "first" match (nearest enemy) without processing
   the entire scene.

8. **Robust Error Handling**: Game engines must gracefully handle edge cases
   like objects being destroyed mid-query, invalid references, or corrupted
   scene state without crashing.

### Developer Experience Expectations

1. **Intuitive Query Patterns**: Developers expect familiar query patterns
   similar to database operations or standard library algorithms - find, filter,
   count, any/all operations.

2. **Composable Operations**: Game logic benefits from combining multiple
   criteria (visible AND tagged as "enemy" AND within range) without performance
   penalties.

3. **Predictable Memory Usage**: Developers need control over memory allocation
   patterns to integrate with custom allocators, object pools, or frame-based
   memory schemes.

4. **Performance Visibility**: Game developers require insights into query costs
   to identify bottlenecks and optimize critical gameplay systems.

5. **Type Safety**: Compile-time validation helps prevent runtime errors that
   could crash shipped games or create hard-to-reproduce bugs.

6. **Integration with Existing Workflows**: The query system should work
   seamlessly with existing scene management, component systems, and debugging
   tools.

### Scalability Requirements

1. **Scene Size Independence**: Query performance should scale gracefully from
   small test scenes (dozens of objects) to large open worlds (thousands of
   objects).

2. **Hierarchy Depth Tolerance**: Deep object hierarchies (UI systems, skeletal
   animations, complex props) should not degrade query performance
   significantly.

3. **Future Optimization Support**: The design should accommodate future
   performance improvements like spatial indexing, caching layers, or
   specialized data structures without API changes.

## Design Goals vs Implementation

### ✅ Achieved Goals

| Goal | Implementation | Notes |
|------|----------------|-------|
| Zero-Copy Performance | ✅ Complete | SceneNode references, no data copying |
| Early Termination | ✅ Complete | `VisitResult::kStop` in FindFirst/Any |
| User-Controlled Allocation | ✅ Complete | Container parameters in all collection methods |
| Leverage Existing Infrastructure | ✅ Complete | Built on const-correct `SceneTraversal<const Scene>` |
| Architectural Consistency | ✅ Complete | Follows `Scene::Traverse()` pattern with const correctness |
| Robust Error Handling | ✅ Complete | `weak_ptr` validation, noexcept design |
| Performance Monitoring | ✅ Complete | `nodes_examined`/`nodes_matched` metrics |
| Google C++ Standards | ✅ Complete | Consistent naming, concepts usage, const correctness |
| **Const Correctness** | ✅ **Complete** | **Compile-time immutability via `ConstVisitedNode`** |
| **Scene Integration** | ✅ **Complete** | **`Scene::Query()` follows established engine patterns** |

### 🔄 Partially Achieved Goals

| Goal | Status | What's Missing |
|------|--------|----------------|
| Clear Separation of Concerns | 🔄 Mostly | Path queries not yet implemented |
| Extensibility | 🔄 Framework Ready | Path system needs completion |

### ❌ Goals Not Yet Addressed

| Goal | Status | Blocker |
|------|--------|---------|
| Complete Path Query Support | ❌ Deferred | Complex parsing logic not implemented |

## Design Evolution

The implementation focused on the core predicate-based query system first,
establishing:

1. **Solid Foundation**: Dual execution modes with clean routing
2. **Performance First**: Single traversal batching with early termination
3. **Memory Safety**: User-controlled allocation patterns
4. **Type Safety**: Template predicates with concept constraints
5. **Const Correctness**: Complete immutability guarantees via
   `SceneTraversal<const Scene>`

### Recent Const Correctness Overhaul

**Major Enhancement**: SceneTraversal underwent a comprehensive const
correctness redesign:

- **Template Specialization**: `SceneTraversal<const Scene>` for read-only
  operations
- **Visitor Type Deduction**: Automatic `ConstVisitedNode` vs
  `MutableVisitedNode` selection
- **Concept-Based Validation**: Compile-time enforcement of const correctness
- **Query Immutability**: All SceneQuery operations are now guaranteed read-only

**Impact on SceneQuery**:
- Enhanced type safety with zero runtime overhead
- Clear separation between read-only queries and mutating operations
- Compile-time validation prevents accidental scene modifications
- Better integration with const Scene instances

**Strategic Decision**: Path queries were deferred to focus on a robust,
const-correct core system rather than attempting everything simultaneously.

## Selected Design: Single Entry Point with Performance Focus

### Design Philosophy

Following the established pattern in the codebase where `Scene::Traverse()`
provides access to a rich `SceneTraversal` interface, we implemented a single
entry point approach:

```cpp
// Single entry point (implemented in Scene class)
auto Query() const -> SceneQuery;
```

**Implementation Status**: ✅ **Complete** - The `Scene::Query()` method is now implemented and returns a `SceneQuery` instance configured for the scene.

### Implemented API Design - Core Features

```cpp
class SceneQuery {
public:
  // Core search operations implemented with const correctness
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto FindFirst(Predicate&& pred) const noexcept -> std::optional<SceneNode>;

  template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
  auto Collect(Container& container, Predicate&& pred) const noexcept
      -> QueryResult;

  template <std::predicate<const VisitedNode&> Predicate>
  auto Count(Predicate&& pred) const noexcept -> QueryResult;

  template <std::predicate<const VisitedNode&> Predicate>
  auto Any(Predicate&& pred) const noexcept -> std::optional<bool>;

  // Path-based queries (core functionality for game engine workflows)
  auto FindFirstByPath(std::string_view path) const noexcept
      -> std::optional<SceneNode>;
  auto FindFirstByPath(
      const SceneNode& context, std::string_view relative_path) const noexcept
      -> std::optional<SceneNode>;

  template <typename Container>
  auto CollectByPath(Container& container,
      std::string_view path_pattern) const noexcept -> QueryResult;

  // Batch execution - execute multiple queries in single traversal pass
  template <typename BatchFunc>
  auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;

private:
  const SceneTraversal& GetTraversal() const noexcept;
  std::weak_ptr<const Scene> scene_;

  // Batch execution state (mutable for const methods)
  mutable std::vector<BatchOperation> batch_operations_;
  mutable bool batch_active_ = false;

  // Internal batch management - 4-phase approach
  auto BatchBegin() const noexcept -> void;

  template <typename BatchFunc>
  auto CreateCompositeFilter(BatchFunc&& batch_func) const noexcept -> auto;

  template <typename CompositeFilter>
  auto Execute(CompositeFilter&& composite_filter) const noexcept
      -> TraversalResult;

  auto BatchEnd(const TraversalResult& traversal_result) const noexcept
      -> BatchResult;

  // Private helper for batch execution visitor
  auto ProcessBatchedNode(const VisitedNode& visited, const Scene& scene,
      bool dry_run) const noexcept -> VisitResult;

  // Private helper for batch operation setup
  template <typename Container>
  QueryResult ExecuteBatchCollect(Container& container,
      const std::function<bool(const VisitedNode&)>& predicate) const noexcept;
  template <typename Container>
  QueryResult ExecuteBatchCollectByPath(
      Container& container, std::string_view path_pattern) const noexcept;
  auto ExecuteBatchFindFirst(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept
      -> std::optional<SceneNode>;
  QueryResult ExecuteBatchCount(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept;
  std::optional<bool> ExecuteBatchAny(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept;

  // Private helper for immediate execution
  template <typename Container>
  QueryResult ExecuteImmediateCollect(Container& container,
      const std::function<bool(const VisitedNode&)>& predicate) const noexcept;
  template <typename Container>
  QueryResult ExecuteImmediateCollectByPath(
      Container& container, std::string_view path_pattern) const noexcept;
  auto ExecuteImmediateFindFirst(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept
      -> std::optional<SceneNode>;
  QueryResult ExecuteImmediateCount(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept;
  auto ExecuteImmediateAny(
      const std::function<bool(const VisitedNode&)>& pred) const noexcept
      -> std::optional<bool>;
};

// Batch result type
struct BatchResult {
    std::size_t nodes_examined = 0;
    std::size_t total_matches = 0;
    bool completed = true;

    explicit operator bool() const noexcept { return completed; }
};

// Minimal result type for current needs
struct QueryResult {
    std::size_t nodes_examined = 0;
    std::size_t nodes_matched = 0;
    bool completed = true;

    explicit operator bool() const noexcept { return completed; }
};
```

### Scene Integration Usage Examples

With the implemented `Scene::Query()` method, the SceneQuery API integrates seamlessly into existing workflows:

```cpp
// Basic integration - get query interface from scene
auto scene = std::make_shared<Scene>("GameWorld");
auto query = scene->Query();

// Single query operations
auto player = query.FindFirst([](const auto& visited) {
  return visited.node.GetName() == "Player";
});

std::vector<SceneNode> enemies;
auto result = query.Collect(enemies, [](const auto& visited) {
  return visited.node.GetName().starts_with("Enemy");
});

// Batch operations for performance
auto batch_result = query.ExecuteBatch([&](auto& q) {
  player = q.FindFirst(player_predicate);
  auto enemy_count = q.Count(enemy_predicate);
  auto has_powerups = q.Any(powerup_predicate);
});

// Const-correct usage with const scenes
void AnalyzeScene(const std::shared_ptr<const Scene>& scene) {
  auto query = scene->Query(); // Returns SceneQuery for const Scene
  // All query operations are guaranteed read-only
  auto stats = query.Count([](const auto& visited) { return true; });
}
```

## Batch Query Execution

The batch execution capability allows multiple queries to be executed in a
single SceneTraversal pass, providing significant performance benefits for
multi-query scenarios.

**Design Decision**: Path-based queries (`FindFirstByPath`, `CollectByPath`) are
**intentionally excluded** from batch execution by design. Path queries use
optimized direct navigation algorithms that are fundamentally incompatible with
the traversal-based batching approach. Attempting to use path queries within
`ExecuteBatch` will trigger an immediate batch rejection/assertion. Path queries
must be executed individually for optimal performance.

### Core Batch Concept

The `ExecuteBatch` method accepts a lambda that receives a query interface. When
called within the lambda, query methods store operations instead of executing
immediately. After the lambda completes, all operations execute in a single
optimized traversal.

```cpp
// Key benefit: N queries → 1 traversal instead of N traversals
auto result = query.ExecuteBatch([&](auto& q) {
    player = q.FindFirst(player_predicate);      // Operation 1
    q.Collect(enemies, enemy_predicate);         // Operation 2
    count = q.Count(pickup_predicate);           // Operation 3
    found = q.Any(explosion_predicate);          // Operation 4
}); // Single traversal executes all 4 operations
```

### Batch Operation Storage

```cpp
// Batch operation storage for building composite filter
struct BatchOperation {
    std::function<bool(const VisitedNode&)> predicate;
    enum class Type { FindFirst, Collect, Count, Any } type;
    void* result_destination;  // Type-erased destination for results
    std::function<void(const VisitedNode&)> result_handler; // Store result logic
    bool has_terminated = false;  // Track if this operation should stop
    mutable bool matched_current_node = false;  // Flag for current node processing
};
```

### Key Batch Execution Benefits

1. **Performance Multiplication**: N queries execute in 1 traversal instead of N
   traversals
2. **Efficient Filtering**: Each predicate tested once per node, results flagged
3. **Smart Termination**: Stops when all FindFirst/Any operations complete
4. **Cache Efficiency**: Single pass maximizes cache locality
5. **Zero Overhead**: When not in batch mode, behaves exactly like immediate
   queries
6. **Type Safety**: Lambda capture maintains full type safety
7. **Memory Control**: User manages all container allocations upfront

### Batch Performance Characteristics

- **Time Complexity**: O(n) for n nodes regardless of number of queries in batch
- **Memory**: User-controlled allocation for all result containers
- **Cache Performance**: Single traversal maximizes cache hit rates
- **Scalability**: Performance improvement scales linearly with number of
  queries

### When to Use Batch Execution

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

## Design Decisions & Rationale

### Path Query Exclusion from Batch Operations

**Decision**: Path-based queries are architecturally incompatible with batch
execution.

**Rationale**:
- Path queries use optimized direct navigation (O(depth) complexity)
- Batch operations require full traversal (O(n) complexity)
- Mixing navigation patterns would eliminate performance benefits
- Implementation complexity would significantly increase without proportional
  benefit

**Impact**:
- Path queries must be executed individually
- Batch operations limited to predicate-based queries only
- Clear separation of concerns between navigation and traversal patterns

### Single Traversal Batch Architecture

**Decision**: All batch operations execute in a single composite traversal pass
with const correctness.

**Rationale**:
- Eliminates the N-traversals problem for N queries
- Maximizes cache locality and memory efficiency
- Provides predictable performance characteristics
- Scales linearly regardless of batch query count
- Maintains compile-time immutability guarantees

### Const Correctness Architecture

**Decision**: Complete const correctness via template specialization and
automatic type deduction.

**Rationale**:
- `SceneTraversal<const Scene>` enforces read-only operations at compile time
- `VisitedNodeT<std::is_const_v<SceneT>>` automatically deduces correct visitor
  node type
- Template concepts validate const correctness without runtime overhead
- Clear separation between querying (read-only) and modification operations

**Implementation Benefits**:
- Zero runtime cost for const correctness validation
- Prevents accidental scene mutations during queries
- Better integration with const Scene instances
- Enhanced type safety and developer experience

## Path Query Syntax

The path-based queries support familiar game engine conventions:

```cpp
// Absolute paths (from scene root)
auto player = query.FindFirstByPath("World/Player");
auto weapon = query.FindFirstByPath("World/Player/Equipment/Weapon");

// Relative paths (from specific node context)
auto weapon = query.FindFirstByPath(player, "Equipment/Weapon");
auto inventory = query.FindFirstByPath(player, "UI/Inventory");

// Wildcard patterns for collection queries
std::vector<SceneNode> all_enemies;
query.CollectByPath(all_enemies, "*/Enemy");        // Direct children named Enemy
query.CollectByPath(all_enemies, "**/Enemy");       // Any Enemy at any depth
query.CollectByPath(all_enemies, "Level/*/Enemy");  // Enemy under any direct child of Level

// Multiple results with wildcards
std::vector<SceneNode> all_weapons;
query.CollectByPath(all_weapons, "**/Weapon");      // All weapons in scene
query.CollectByPath(all_weapons, "Player/*/Weapon"); // All weapons under Player's children
```

## Usage Examples (Current Implementation)

### ✅ Working Examples (Const-Correct Implementation)

```cpp
// NOTE: Scene::Query() not yet implemented - assume query object exists
auto& query = scene.Query(); // TODO: Add to Scene class

// ✅ Const-correct predicate-based queries (IMPLEMENTED)
if (auto player = query.FindFirst([](const ConstVisitedNode& visited) {
    // visited.node_impl is const SceneNodeImpl* - no mutations possible
    return visited.node_impl->GetName() == "Player";
})) {
    // Use player node - already validated by Scene API
}

// ✅ User-controlled allocation with const-correct predicates (IMPLEMENTED)
std::vector<SceneNode> enemies;
enemies.reserve(50); // User manages memory
auto result = query.Collect(enemies, [](const ConstVisitedNode& visited) {
    // Compile-time const correctness enforced
    return visited.node_impl->HasTag("enemy");
});

// ✅ Count and Any operations with const correctness (IMPLEMENTED)
auto enemy_count = query.Count([](const ConstVisitedNode& v) {
    // v.node_impl->Mutate(); // ❌ Compile error - const correctness enforced
    return v.node_impl->HasTag("enemy");
});

auto has_player = query.Any([](const ConstVisitedNode& v) {
    return v.node_impl->GetName() == "Player";
});

// ✅ Const-correct batch execution (FRAMEWORK IMPLEMENTED, collect incomplete)
SceneNode player;
size_t destructible_count = 0;
bool has_explosions = false;

auto batch_result = query.ExecuteBatch([&](auto& q) {
    // ✅ These work in batch mode with const correctness
    player = q.FindFirst([](const ConstVisitedNode& v) {
        return v.node_impl->GetName() == "Player";
    }).value_or(SceneNode{});

    auto count_result = q.Count([](const ConstVisitedNode& v) {
        // Const-correct access - no scene mutations possible
        return v.node_impl->HasTag("destructible");
    });
    destructible_count = count_result.nodes_matched;

    has_explosions = q.Any([](const ConstVisitedNode& v) {
        return v.node_impl->HasTag("explosion");
    }).value_or(false);

    // ❌ This doesn't work yet - ExecuteBatchCollect is empty
    // std::vector<SceneNode> enemies;
    // q.Collect(enemies, enemy_predicate); // Not implemented

    // ❌ Path queries not supported in batches (by design)
    // auto weapon = q.FindFirstByPath("Player/Weapon"); // Will fail
});

if (batch_result) {
    std::cout << "Batch examined " << batch_result.nodes_examined << " nodes\n";
    std::cout << "Found " << batch_result.total_matches << " total matches\n";
}
```

### 🔒 Const Correctness Benefits

```cpp
// ✅ Guaranteed read-only operations
query.FindFirst([](const ConstVisitedNode& visited) {
    auto name = visited.node_impl->GetName();     // ✅ Read access OK
    auto flags = visited.node_impl->GetFlags();   // ✅ Read access OK
    // visited.node_impl->SetName("New");         // ❌ Compile error
    // visited.node_impl->SetFlags(flags);        // ❌ Compile error
    return name == "Target";
});

// ✅ Type safety with automatic const deduction
SceneTraversal<const Scene> traversal(scene);    // Read-only traversal
// Uses ConstVisitedNode automatically based on Scene template parameter
```

### ❌ Not Yet Working Examples

```cpp
// ❌ Path-based queries (NOT IMPLEMENTED - commented out)
// auto weapon = query.FindFirstByPath("World/Player/Equipment/Weapon");
// auto inventory = query.FindFirstByPath(player, "Equipment/Inventory");

// ❌ Wildcard path queries (NOT IMPLEMENTED)
// std::vector<SceneNode> all_weapons;
// query.CollectByPath(all_weapons, "**/Weapon");

// ✅ Scene integration (IMPLEMENTED)
auto query = scene->Query(); // Scene::Query() returns SceneQuery instance
```

### Architectural Constraints

**Batch Mode Limitation**: Path-based queries are fundamentally incompatible
with batch execution. The batch system is designed around full scene traversal
with composite filtering, while path queries use direct navigation algorithms
optimized for specific hierarchical access patterns. This architectural
constraint is enforced at runtime to prevent performance degradation and
maintain clear separation between navigation and search operations.

## Key Implementation Insights

1. **Exception-Free Design**: SceneTraversal is completely exception-free,
   eliminating all try/catch overhead. Error handling uses return values and
   optional types exclusively.

2. **Guaranteed Valid Node Access**: SceneTraversal's `UpdateNodeImpl` ensures
   that all filters and visitors receive valid `node_impl` pointers, eliminating
   the need for null checks in query implementations.

3. **Immutable Query Operations**: All query operations are read-only and do not
   modify the scene graph during traversal, ensuring thread-safety and
   consistency.

4. **Filtering Integration**: Custom filters integrate predicate logic directly
   into SceneTraversal's filtering system, providing:
   - Zero double-lambda overhead
   - Optimal SceneTraversal performance
   - Early subtree rejection via `FilterResult::kRejectSubTree`
   - Accurate node examination metrics

5. **Performance Optimization Strategies**:
   - Simple absolute paths use direct parent-child navigation
   - Complex wildcard patterns use filtered traversal
   - Aggressive early termination via `VisitResult::kStop`
   - Smart subtree rejection for path patterns

6. **Robust Error Handling**:
   - Minimal weak_ptr checks for scene lifetime
   - UpdateNodeImpl validation handled by SceneTraversal
   - Graceful degradation with partial results
   - TraversalResult provides completion status

7. **Memory Efficiency**:
   - Zero allocations during traversal operations
   - User-controlled container allocation via `CollectTo`
   - Direct SceneNodeImpl* access through VisitedNode
   - Pre-allocated internal buffers in SceneTraversal

## Performance Characteristics

1. **FindFirst Operations**: O(1) to O(n) with aggressive early termination,
   typically sub-millisecond for common queries
2. **CollectTo Operations**: O(n) with user-controlled allocation, optimal cache
   locality through SceneTraversal
3. **Path Navigation**: O(depth) for simple paths, O(n) for wildcard patterns
   with early subtree rejection
4. **Count/Any Operations**: O(n) worst case, with early termination for Any
   queries

**Implementation Note**: SceneQuery achieves maximum performance by:

1. **Leveraging SceneTraversal's Architecture**: Exception-free operation,
   optimized filtering, cache-friendly traversal
2. **Smart Algorithm Selection**: Direct navigation for simple cases, filtered
   traversal for complex patterns
3. **Aggressive Optimization**: Early termination, subtree rejection, minimal
   allocations
4. **Existing Infrastructure**: Scene SafeCall validation, proven SceneTraversal
   performance patterns

## Potential Concerns & Mitigations

### 1. Query Performance vs Linear Search
**Concern**: Complex predicates on unindexed searches could be slow for large
scenes. **Mitigation**:
- QueryResult provides performance metrics for optimization decisions
- Built-in methods (FindByName) leverage existing Scene optimizations
- Future spatial/component indexing can be added transparently
- FilterResult::kRejectSubTree enables aggressive pruning

### 2. Scene Lifetime Management
**Concern**: Scene destruction during queries could cause issues.
**Mitigation**:
- Weak pointer checks prevent access to destroyed scenes
- SceneNode creation uses shared_ptr for automatic safety
- SceneTraversal handles all node-level lifetime validation
- Graceful degradation with partial results

### 3. Path Parsing Overhead
**Concern**: String parsing for path queries might impact performance.
**Mitigation**:
- Simple absolute paths bypass parsing with direct navigation
- Path parsing can be cached for repeated patterns
- FilterResult::kRejectSubTree reduces traversal cost
- Future path compilation to predicate functions

### 4. Predicate Complexity
**Concern**: Complex user predicates might dominate query time. **Mitigation**:
- SceneTraversal's filtering minimizes predicate calls
- Built-in convenience methods for common patterns
- Performance monitoring identifies expensive predicates
- Future caching for expensive predicate results

## Conclusion

This SceneQuery design successfully delivers a high-performance, zero-copy query
system that:

1. **Maximizes Performance**: Leverages SceneTraversal's exception-free,
   optimized traversal with intelligent filtering integration
2. **Ensures Safety**: Inherits Scene's proven SafeCall patterns without
   duplicating validation logic
3. **Provides Flexibility**: Core predicate system supports all future query
   scenarios through extensible design
4. **Maintains Consistency**: Follows established engine patterns with
   `Scene::Query()` → `SceneQuery` architecture
5. **Enables Growth**: Solid foundation for spatial indexing, component queries,
   and real-time query management

The implementation achieves game engine performance requirements while providing
an intuitive, safe API that scales from simple name lookups to complex spatial
queries. The careful integration with existing SceneTraversal infrastructure
ensures maximum performance with minimal development risk.

---

## Appendix: Alternate Design Options Considered

### Option 1: Enhanced Direct API on Scene Class
Add many specialized methods directly to the Scene class:
```cpp
template<typename Predicate>
auto FindNodes(Predicate&& predicate) const -> std::vector<SceneNode>;

auto FindNodesByTag(std::string_view tag) const -> std::vector<SceneNode>;
auto FindNodesInSubtree(const SceneNode& root) const -> std::vector<SceneNode>;
auto FindNodesByPath(std::string_view path) const -> std::vector<SceneNode>;
// ... many more methods
```

**Pros:**
- Simple to use
- Direct access

**Cons:**
- Clutters Scene API with many methods
- Always allocates vectors (performance issue)
- Not extensible without modifying Scene class
- Doesn't follow established patterns in the codebase

### Option 2: Builder Pattern Query System
Separate query builder class with fluent interface:
```cpp
auto results = scene.Query()
    .WithTag("interactive")
    .InSubtree(level_root)
    .WithDepthRange(2, 5)
    .Where([](const auto& node) { return node.IsVisible(); })
    .FindAll();
```

**Pros:**
- Flexible and composable
- Clean separation of concerns
- Type-safe

**Cons:**
- Still forces allocation of result vectors
- More complex implementation
- Doesn't leverage existing traversal infrastructure

### Option 3: True std::ranges Integration
Design that provides iterators/ranges working directly with standard library
algorithms:
```cpp
class SceneQuery {
public:
    auto begin() const -> scene_iterator;
    auto end() const -> scene_iterator;
    // Works with std::views::filter, std::views::transform, etc.
};

// Usage:
auto& query = scene.Query();
auto enemies = query
    | std::views::filter([](const auto& node) { return node.HasTag("enemy"); })
    | std::views::filter([](const auto& node) { return node.IsVisible(); })
    | std::ranges::to<std::vector>();
```

**Pros:**
- Full compatibility with C++20 ranges and algorithms
- Composable with standard library
- Familiar to modern C++ developers
- Lazy evaluation potential

**Cons:**
- Complex iterator infrastructure required
- Doesn't leverage existing SceneTraversal system effectively
- Performance overhead from generic iterator abstraction
- Still results in allocations for many use cases
- Iterator invalidation concerns with scene modifications

### Option 4: Single Entry Point with Performance Focus
Following the established pattern where `Scene::Traverse()` provides access to a
rich `SceneTraversal` interface:

```cpp
// Single entry point for targeted node searches (distinct from Traverse())
auto Query() const -> const SceneQuery&;

class SceneQuery {
public:
    // Random access search with early termination when target found
    template<std::predicate<const VisitedNode&> Predicate>
    auto FindFirst(Predicate&& pred) const -> std::optional<SceneNode>;

    // Collect specific matching nodes to user-provided container
    template<typename Container, std::predicate<const VisitedNode&> Predicate>
    auto Collect(Container& container, Predicate&& pred) const -> void;

    // Count matching nodes without allocation
    template<std::predicate<const VisitedNode&> Predicate>
    auto Count(Predicate&& pred) const -> size_t;

    // Check existence of matching nodes without allocation
    template<std::predicate<const VisitedNode&> Predicate>
    auto Any(Predicate&& pred) const -> bool;
};
```

**Pros:**
- Zero-copy performance for targeted searches
- Reuses existing node access infrastructure (not traversal optimization)
- User-controlled allocation for results
- Early termination when specific nodes are found
- Follows established codebase patterns
- Simple implementation

**Cons:**
- Less familiar API compared to standard library patterns
- Requires learning domain-specific interface
