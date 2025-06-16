# SceneQuery Batch Collection Safety Analysis

## The Problem: Unsafe Batch Collections with User Containers

### Root Cause

The current SceneQuery batch system has a fundamental safety flaw when used with user-provided containers for collection operations. The issue stems from **deferred execution with reference capture**:

```cpp
// UNSAFE: User container + batch mode = ASan corruption
auto result = query.ExecuteBatch([&](auto& q) {
    std::vector<SceneNode> my_container;  // User-owned container
    q.Collect(my_container, predicate);   // Captures container by reference
    // Deferred execution later calls my_container.emplace_back()
    // Vector reallocation corrupts internal metadata -> ASan error
});
```

### Technical Details

1. **Reference Capture**: `ExecuteBatchCollect` stores lambda `[&](const SceneNode& node) { container.emplace_back(node); }`
2. **Deferred Execution**: Lambda is stored in `batch_operations_` and executed during later traversal
3. **Vector Reallocation**: During traversal, `emplace_back()` triggers reallocation, invalidating internal pointers
4. **ASan Detection**: Memory sanitizer detects corruption when vector metadata becomes inconsistent

## The Solution: Hybrid API Design

### Safe Pattern 1: Immediate Mode with User Containers

```cpp
// ✅ SAFE: Immediate execution, no deferred reference capture
std::vector<SceneNode> my_enemies;
auto result = query.Collect(my_enemies, enemy_predicate);
```

### Safe Pattern 2: Batch Mode with Query-Owned Collections

```cpp
// ✅ SAFE: Query owns collections, returns via move semantics
auto batch_results = query.ExecuteBatch([](auto& q) {
    return std::make_tuple(
        q.Collect(enemy_predicate),      // Returns std::vector<SceneNode>
        q.Count(visible_predicate),      // Returns QueryResult
        q.FindFirst(player_predicate)    // Returns std::optional<SceneNode>
    );
});
```

### Unsafe Pattern: Batch Mode with User Containers

```cpp
// ❌ UNSAFE: Deferred execution + reference capture = corruption
auto result = query.ExecuteBatch([&](auto& q) {
    std::vector<SceneNode> my_container;
    q.Collect(my_container, predicate);  // FORBIDDEN PATTERN
});
```

## API Design Principles

### 1. Memory Ownership Clarity

- **Immediate Mode**: Caller owns containers, immediate execution
- **Batch Mode**: Query system owns collections until return, then moves to caller
- **Benefit**: Eliminates ambiguous ownership during async execution

### 2. Performance vs. Safety Trade-offs

- **User Containers**: Maximum allocation control, but unsafe in batch mode
- **Query Collections**: Safe async execution, with reasonable allocation patterns
- **Hybrid**: Best of both worlds with clear safety boundaries

### 3. Deferred Execution Power Preservation

Batch mode still provides core benefits:

- **Single Traversal**: N queries = 1 scene pass instead of N passes
- **Cache Efficiency**: Maximize temporal locality of scene data access
- **Early Termination**: Stop when all FindFirst/Any operations complete
- **Composite Filtering**: Union of all predicates for optimal traversal

## Implementation Strategy: Making Unsafe Patterns Impossible

### Option 1: Runtime Checks with Clear Error Messages

```cpp
template<typename Container, typename Predicate>
auto Collect(Container& container, Predicate pred) -> QueryResult {
    if (batch_active_) {
        throw std::logic_error("User containers forbidden in batch mode. Use query-owned Collect(predicate) instead.");
    }
    return ExecuteImmediateCollect(container, pred);
}

// Batch mode supports query-owned collections
template<typename Predicate>
auto Collect(Predicate pred) -> std::vector<SceneNode> {
    if (batch_active_) return ExecuteBatchCollectOwned(pred);
    return ExecuteImmediateCollectOwned(pred);
}
```

### Option 2: Separate Method Names (KISS Winner)

```cpp
// Immediate mode only - clear from name
template<typename Container, typename Predicate>
auto CollectInto(Container& container, Predicate pred) -> QueryResult {
    assert(!batch_active_);  // Debug-only check
    return ExecuteImmediateCollect(container, pred);
}

// Mode-agnostic - returns query-owned collection
template<typename Predicate>
auto Collect(Predicate pred) -> std::vector<SceneNode> {
    if (batch_active_) return ExecuteBatchCollectOwned(pred);
    return ExecuteImmediateCollectOwned(pred);
}
```

### Option 3: Type-Based Disambiguation

```cpp
template<typename T>
struct user_container_ref { T& container; };

template<typename Container>
auto into(Container& c) { return user_container_ref<Container>{c}; }

// Clear ownership semantics through types
template<typename Container, typename Predicate>
auto Collect(user_container_ref<Container> wrapper, Predicate pred) -> QueryResult {
    if (batch_active_) {
        throw std::logic_error("User containers not supported in batch mode");
    }
    return ExecuteImmediateCollect(wrapper.container, pred);
}

template<typename Predicate>
auto Collect(Predicate pred) -> std::vector<SceneNode> {
    if (batch_active_) return ExecuteBatchCollectOwned(pred);
    return ExecuteImmediateCollectOwned(pred);
}
```

### Option 4: Explicit Mode Tags

```cpp
struct immediate_mode_t {};
inline constexpr immediate_mode_t immediate_mode{};

// Explicit opt-in for user containers
template<typename Container, typename Predicate>
auto Collect(immediate_mode_t, Container& container, Predicate pred) -> QueryResult {
    return ExecuteImmediateCollect(container, pred);
}

// Default: query-owned collections
template<typename Predicate>
auto Collect(Predicate pred) -> std::vector<SceneNode> {
    if (batch_active_) return ExecuteBatchCollectOwned(pred);
    return ExecuteImmediateCollectOwned(pred);
}
```

## Recommended Implementation: Option 2 (Separate Method Names)

### Why This Is The Simplest Valid Solution

1. **Compile-Time Safety**: Impossible to call `CollectInto` in batch mode (method doesn't exist on batch query interface)
2. **Clear Intent**: Method names explicitly indicate ownership model (`CollectInto` vs `Collect`)
3. **No Runtime Overhead**: No need for runtime checks or exception handling
4. **Valid C++20**: Uses standard overloading, no invalid `requires` on runtime variables

### Implementation Details

```cpp
class SceneQuery {
    // Immediate mode: populate user container
    template<typename Container, typename Predicate>
    auto CollectInto(Container& container, Predicate pred) -> QueryResult {
        assert(!batch_active_);  // Debug-only check
        return ExecuteImmediateCollect(container, pred);
    }

    // Any mode: return query-owned collection
    template<typename Predicate>
    auto Collect(Predicate pred) -> std::vector<SceneNode> {
        if (batch_active_) return ExecuteBatchCollectOwned(pred);
        return ExecuteImmediateCollectOwned(pred);
    }
};
```

### Usage Patterns

```cpp
// Immediate mode - both patterns work
std::vector<SceneNode> my_container;
auto result1 = query.CollectInto(my_container, pred);  // Clear: uses caller container
auto nodes = query.Collect(pred);                     // Clear: returns new collection

// Batch mode - only query-owned collections work
auto batch_results = query.ExecuteBatch([](auto& q) {
    // This method doesn't exist on batch query interface:
    // q.CollectInto(container, pred);  // Compile error

    return q.Collect(pred);  // Only this method available
});
```

## Game Engine Architecture Benefits

### 1. Performance Predictability

- Immediate mode: Predictable allocation timing under caller control
- Batch mode: Centralized allocation patterns, easier to profile and optimize

### 2. Memory Budget Control

- Query system can manage memory budgets centrally in batch mode
- Critical for console/mobile memory constraints

### 3. Debugging and Profiling

- Centralized allocation in query system for batch operations
- Easier to profile and optimize scene query memory usage

### 4. Type Safety and Ergonomics

```cpp
// Natural API progression
auto enemies = query.Collect(enemy_pred);           // Simple case
auto result = query.Collect(my_vector, enemy_pred); // Performance tuning
```

## Test Case Corrections

The failing tests were using the unsafe pattern:

```cpp
// OLD: Unsafe batch collection with user container
query->ExecuteBatch([&](const auto& q) {
    std::vector<SceneNode> all_nodes;
    q.Collect(all_nodes, predicate);  // ASan corruption
});

// NEW: Safe immediate collection
std::vector<SceneNode> all_nodes;
auto result = query->Collect(all_nodes, predicate);

// OR: Safe batch collection with query-owned containers
auto batch_results = query->ExecuteBatch([](const auto& q) {
    return q.Collect(predicate);  // Returns std::vector<SceneNode>
});
```

## Action Items

1. **Implement SFINAE constraints** to make unsafe patterns impossible
2. **Add comprehensive tests** for both safe patterns
3. **Update documentation** to clearly explain the hybrid model
4. **Performance benchmark** both approaches to validate design decisions
5. **Consider future async support** with query-owned collections
