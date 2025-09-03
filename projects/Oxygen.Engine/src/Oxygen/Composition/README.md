# Oxygen Engine Composition System

## Table of Contents

- [Implementation Status](#implementation-status)
- [Overview](#overview)
- [The Global Type System](#the-global-type-system)
- [Resource Types List](#resource-types-list)
- [ComponentPool](#componentpool)
- [The Global Component Pool Registry](#the-global-component-pool-registry)
- [Hybrid Storage Architecture and Usage](#hybrid-storage-architecture-and-usage)
- [Copy, Move, and Clone Semantics](#copy-move-and-clone-semantics)
- [Error Handling](#error-handling)
- [Diagnostics: Printing and Logging Components](#diagnostics-printing-and-logging-components)
- [Composition Capacity Tuning](#composition-capacity-tuning)
- [Performance Benchmarks](#performance-benchmarks)

## Implementation Status

| Done | Description |
|:----:|-------------|
| ✅ | Implement template metaprogramming resource type system using TypeList and IndexOf |
| ✅ | Update Resource.h with compile-time type ID generation system |
| ✅ | Design centralized ResourceTypeList for consistent type ID allocation |
| ✅ | Create comprehensive test suite for Resource template metaprogramming system |
| ✅ | Implement thread-safe ComponentPool template with compile-time type resolution and contiguous storage |
| ✅ | Implement ComponentPoolRegistry class that manages ComponentPool instances using GetTrulySingleInstance |
| ✅ | Add ComponentPoolRegistry initialization function to cs_init.cpp for global singleton system |
| ✅ | Refactor `Composition` for hybrid storage: add dual containers (unique and pooled) |
| ✅ | Implement compile-time storage selection: Update `AddComponent`, `GetComponent`, `RemoveComponent`, and related APIs to use `if constexpr` and type traits to select the correct storage. |
| ✅ | Update component declaration macros: Ensure `OXYGEN_COMPONENT` and (if needed) `OXYGEN_RESOURCE_COMPONENT` macros correctly detect and register pooled vs. non-pooled components. |
| ✅ | Integrate pooled component access: Route pooled component operations through `ComponentPoolRegistry` and ensure handle validation and error handling are robust. |
| ✅ | Implement and test dependency-aware removal: Prevent removal of components that are required by others, and ensure dependency resolution works for both storage types. |
| ✅ | Write and run unit tests for hybrid storage: Cover all scenarios—adding, removing, accessing, and dependency management for both pooled and non-pooled components. |
| ✅ | Benchmark pooled vs. non-pooled performance: Measure and compare access times, memory usage, and cache locality for both storage types. |
| ✅ | Document usage guidelines and performance recommendations: Provide clear documentation for engine users on when and how to use pooled vs. non-pooled components, and how to declare new components. |

## Overview

This document outlines the design of Oxygen composition system. The system is
designed for the specific needs of a game engine, that manages entities,
components, resources, etc. which need unique type identifiers, but where RTTI
would bring too much overhead.

The engine makes clear distinction between different categories of objects it
manipulates:

**Objects** (inherit from `Object`)

- Have a `TypeId` and a type name

**Components** (inherit from `Component`, which extends `Object`)

- Represent a data-oriented part of a larger object
- Can be composed into a `Composition`
- Can use different storage strategies:
  - Pooled components: stored in `ResourceTable`, and require a unique Resource
    Type Id
  - Ad-hoc components: stored directly in the composition, allocated on the
    heap, and do not require a Resource Type Id
- Examples: `ObjectMetadata` (pooled), `TransformComponent` (pooled),
  `DeferredReclaimer` (non pooled)

```cpp
namespace oxygen {
    class Object {
        // Base type system functionality
        virtual TypeId GetTypeId() const = 0;
        virtual const char* GetTypeName() const = 0;
    };

    class Component : public virtual Object {
        // Component-specific functionality
        virtual void UpdateDependencies(const Composition& composition) {}
        virtual bool IsCloneable() const noexcept { return false; }
        virtual std::unique_ptr<Component> Clone() const { /* ... */ }

    protected:
        Component() = default;  // Only Composition can create components
        friend class Composition;
    };
}
```

**Compositions** (inherit from `Composition`, which extends `Object`)

- Built from one or more `Components`, specified at construction time or at
  runtime
- Supports adding, removing and querying for components
- A powerful and more reliable alternative to multiple inheritance
- Examples: `SceneNode`, `Renderer`, `Graphics`

**Resources** (inherit from `Resource<ResourceT, ResourceTypeList>`):

- Use ResourceTable for storage with ResourceHandle-based access
- Have compile-time resource type IDs (limited to 256 types)
- Examples: `SceneNode`, `TransformComponent` (pooled)

## The Global Type System

The Oxygen Engine employs a sophisticated type identification system that
operates without RTTI overhead while maintaining type safety across module
boundaries. At its core lies the `Object` base class, which provides every
derived type with a unique `TypeId` and human-readable type name. This
foundation enables runtime type queries and dynamic dispatch while avoiding the
performance penalties associated with standard RTTI mechanisms.

The type registry operates on a dual-tier system that serves different
architectural needs. For general object identification, the engine maintains a
global type registry that assigns unique identifiers to all `Object`-derived
classes. This registry utilizes compile-time template metaprogramming to ensure
consistent type assignment across compilation units, making it particularly
valuable in multi-DLL environments where traditional RTTI can become unreliable.

The `OXYGEN_TYPED` macro simplifies type registration and provides a
standardized way to declare type-aware objects. When you declare a class with
`OXYGEN_TYPED(ClassName)`, the macro automatically generates the necessary type
identification infrastructure, including static type accessors and proper
integration with the global type registry. This approach eliminates boilerplate
code while ensuring consistent type handling throughout the engine.

For Components specifically, the `OXYGEN_COMPONENT` macro combines
`OXYGEN_TYPED` with component-specific features like friendship with the
`Composition` class and protected constructors to ensure components are only
created through the composition system.

## Resource Types List

The implemented solution uses template metaprogramming instead of runtime mechanisms to ensure:

1. **Zero Runtime Overhead**: All type resolution happens at compile time
2. **No RTTI Dependency**: Works in environments where RTTI is disabled
3. **Cross-Module Consistency**: Type IDs are consistent across DLL boundaries

ComponentPoolRegistry provides a truly global singleton for managing component
pools across all modules:

### Global Resource Type Registration

**MANDATORY**: All resource types (scene nodes, pooled components) must be
registered in the centralized `ResourceTypeList` in `ResourceTypes.h`:

```cpp
// ResourceTypes.h - Global resource type registry (must be included everywhere)
#pragma once

#include <Oxygen/Base/Resource.h>

namespace oxygen {

// Forward declare all resource types that use ResourceTable
class SceneNode;           // Resource: composition of components
class TransformComponent;  // Pooled Component: inherits from Resource
class RenderComponent;     // Pooled Component: inherits from Resource
class PhysicsComponent;    // Pooled Component: inherits from Resource
// ... add all resource types here (scene nodes, pooled components, etc.)

// Note: Non-pooled components (like SettingsComponent, ManagerComponent)
// do NOT need to be Resources and should NOT be in this list

/*!
 Centralized resource type list - ORDER MATTERS!

 This list determines the compile-time resource type IDs:
 - SceneNode gets ID 0
 - TransformComponent gets ID 1
 - RenderComponent gets ID 2
 - PhysicsComponent gets ID 3
 - etc.

 WARNING: Never change the order of existing types in this list!
 Only append new types to the end to maintain binary compatibility.
*/
using ResourceTypeList = TypeList<
    SceneNode,              // Scene graph nodes
    TransformComponent,     // Pooled components only
    RenderComponent,
    PhysicsComponent
    // Add new resource types here at the end only
    // DO NOT add non-pooled components here!
>;

} // namespace oxygen
```

### Binary Compatibility Rules

1. **Never reorder** existing types in `ResourceTypeList`
2. **Never remove** types from `ResourceTypeList`
3. **Only append** new resource types to the end of the list
4. **Always include** `ResourceTypes.h` before using any resource type
5. **Resource Type Limit**: Maximum 256 resource types

## ComponentPool

The `ComponentPool<T>` template provides thread-safe, high-performance pooled
storage for frequently accessed components using ResourceTable as its
foundation. It achieves zero-runtime-overhead type resolution through
compile-time template metaprogramming while maintaining O(1) performance for all
operations.

**Core Architecture**: The pool wraps a ResourceTable with thread-safe
operations, using a shared_mutex to allow concurrent reads while serializing
writes. Components are allocated with perfect forwarding and accessed through
validated ResourceHandle objects that automatically detect stale references.

**Thread Safety Model**: All operations use either shared locks (for reads) or
exclusive locks (for writes). Multiple threads can safely read component data
simultaneously, while allocation, deallocation, and modification operations are
properly serialized.

**Defragmentation Support**: The pool provides two defragmentation modes -
automatic detection of component's static Compare method using C++20 concepts,
or custom comparison functions. This maintains cache locality over time by
compacting the underlying storage and optionally sorting components.

**Handle Validation**: Component access returns nullptr for invalid handles
rather than throwing exceptions, enabling robust error handling in game loops
where components may be deleted between frames.

**Memory Management**: Initial capacity can be specified to avoid reallocations
during gameplay. The pool warns when growth occurs, helping developers optimize
memory usage patterns.

**Type Resolution**: Component resource type IDs are resolved at compile time
using the centralized ResourceTypeList, ensuring consistent type mapping across
DLL boundaries without runtime overhead.

## The Global Component Pool Registry

### Architecture Principles

The **ComponentPoolRegistry** serves as the central coordination point for
managing component pools across the entire engine. Its design follows several
key architectural principles:

**Cross-Module Singleton Pattern**: Utilizes `GetTrulySingleInstance` to ensure
exactly one registry exists across all DLLs and modules. This prevents the
common pitfall where different modules create separate singleton instances,
leading to fragmented pool management and resource inconsistencies.

**Type-Erased Pool Storage**: Manages pools of different component types through
a unified interface using type erasure. Each pool is stored as a void pointer
with associated type-specific deleter and clear functions, allowing the registry
to manage heterogeneous pool types without template proliferation at the
registry level.

**Lazy Pool Creation**: Pools are created on-demand when first accessed rather
than pre-allocated. This reduces memory footprint and startup time while
ensuring pools are only created for component types actually used by the
application.

**Thread-Safe Pool Access**: All pool creation and access operations are
protected by mutex synchronization. Once a pool exists, subsequent accesses
return the same pool instance, enabling safe concurrent access from multiple
threads.

**Configurable Pool Sizing**: Components can specify their expected pool size
through a static `kExpectedPoolSize` member. The registry detects this at
compile-time using C++20 concepts and uses it for initial pool allocation,
optimizing memory usage for different component access patterns.

### Pool Lifecycle Management

The registry manages the complete lifecycle of component pools from creation to
cleanup:

**Pool Creation**: When a component type is first accessed, the registry creates
a new `ComponentPool` instance with appropriate initial capacity. The initial
capacity can be specified, in the class representing the component type stored
in the pool, by having a class member named `kExpectedPoolSize`.

**Pool Reuse**: Subsequent requests for the same component type return the
existing pool instance. This ensures all parts of the application access the
same shared pool for each component type.

**Cleanup Integration**: Each pool stores both a destructor function and a clear
function, enabling the registry to perform both full cleanup (during shutdown)
and state reset (between test runs) operations on type-erased pools.

### Integration Points

The registry integrates with several engine systems:

**Composition System**: Compositions query the registry to access appropriate
pools when creating or accessing pooled components. This provides transparent
pooled storage without exposing pool management complexity to composition users.

**Testing Framework**: The registry provides `ClearAllPools()` functionality to
reset all component pools between test runs, ensuring test isolation and
preventing state pollution across test cases.

**Module Initialization**: The registry is typically initialized during engine
startup through exported C functions that establish the singleton instance in a
predictable manner, ensuring proper cross-module access.

## Hybrid Storage Architecture and Usage

Oxygen Engine's Composition class supports a hybrid storage system, enabling
transparent, high-performance management of both pooled and unique (non-pooled)
components. This architecture leverages C++20 concepts and type traits to
distinguish component storage strategies at compile time, ensuring zero runtime
overhead and a unified API.

### 1. Compile-Time Component Type Distinction

- The concept `PooledComponent<T>` determines if a component is pooled:

  ```cpp
  template <typename T>
  concept PooledComponent = std::is_base_of_v<Resource<T, ResourceTypeList>, T>;
  ```

  If `PooledComponent<T>` is true, T is stored in a global ComponentPool.
  Otherwise, T is stored directly in the composition instance.

### 2. Storage Layout and Automatic Routing

- **Dual containers** in `ComponentManager`:
  - `std::unordered_map<TypeId, ResourceHandle> pooled_components_` for pooled.
  - `ComponentsCollection components_` and `std::unordered_map<TypeId, size_t>
    component_index_` for unique.
- All access is protected by a `mutable std::shared_mutex mutex_` in the
  `Composition` class.
- All major APIs (`AddComponent`, `GetComponent`, `RemoveComponent`) use `if
  constexpr (PooledComponent<T>)` to select the correct storage path. Locking is
  performed in the `Composition` class before any routing or dispatch to
  `ComponentManager`.

**Example for `GetComponent`:**

```cpp
// Template metaprogramming routes to appropriate storage
template<typename T>
auto Composition::GetComponent() -> T& {
    std::shared_lock lock(pimpl_->access_mutex_);
    if constexpr (PooledComponent<T>) {
        auto handle = pimpl_->pooled_components_.at(T::ClassTypeId());
        auto& pool = ComponentPoolRegistry::GetInstance().GetPool<T>();
        return pool.Get(handle); // Throws if handle invalid
    } else {
        return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
    }
}
```

### 3. Component Declaration and Usage

- Use `OXYGEN_COMPONENT(T)` for unique (non-pooled) components.
- Use `OXYGEN_POOLED_COMPONENT(T, ResourceTypeList)` for pooled components. This
  macro both declares the component and associates it with a resource type list
  for pooling.

**Examples:**

```cpp
// Pooled component
class TransformComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(TransformComponent, ResourceTypeList)
  // ...
};

// Unique (non-pooled) component
class SettingsComponent : public oxygen::Component {
  OXYGEN_COMPONENT(SettingsComponent)
  // ...
};
```

### 4. Dependencies Between Components

- Specify component dependencies using the `OXYGEN_COMPONENT_REQUIRES(...)`
  macro inside your component class. This ensures dependencies are declared at
  compile time and validated at runtime.
- Store dependencies as either handles (for pooled) or pointers (for unique),
  using `if constexpr (PooledComponent<T>)`.
- Use the `UpdateDependencies()` method to resolve dependencies after all
  components are constructed.

**Example:**

```cpp
class DependentComponent : public oxygen::Component {
  OXYGEN_COMPONENT(DependentComponent)
  OXYGEN_COMPONENT_REQUIRES(TransformComponent)
public:
  void UpdateDependencies(const Composition& composition) override {
    if constexpr (PooledComponent<TransformComponent>) {
      transform_handle_ = composition.GetComponentHandle<TransformComponent>();
    } else {
      transform_ptr_ = &composition.GetComponent<TransformComponent>();
    }
  }
  // ...
private:
  // Use handle or pointer depending on storage type
  std::conditional_t<PooledComponent<TransformComponent>,
    ResourceHandle, TransformComponent*> transform_dep_;
};
```

- The `OXYGEN_COMPONENT_REQUIRES` macro lists all required component types. The
  system will enforce that these dependencies exist before the component is
  created.
- The `UpdateDependencies` method is called after all dependencies are
  available, allowing you to cache handles or pointers as appropriate.

### 5. Best Practices

- Use pooled components for high-frequency, bulk-accessed data.
- Use unique components for per-entity or rarely accessed data.
- Always use the provided macros and registration lists to ensure correct storage and type safety.
- Avoid storing raw pointers to pooled components; always use handles for pooled types.

## Copy, Move, and Clone Semantics

The `Composition` class supports shallow copy, move, and clone operations. These
allow you to duplicate or transfer entire compositions, including all components
and their dependencies.

- **Copy/Move:**
  - Copy: duplicates an existing composition, but keeps shared ownership of the
    components.
  - Move: transfers all components, preserving dependencies, making the source
    composition empty.

- **Clone:**
  - Clone: deep copies the entire composition, with independent components for
    the source and the target. Requires **all** components to support cloning.
  - Use the `CloneableMixin` CRTP to enable deep cloning for your custom
    composition types.
  - The `Clone()` method returns a `std::unique_ptr` to a new, deep-copied
    composition.

**Thread Safety:** Copy, move, and clone operations are *not* thread-safe. You
must ensure exclusive access to the composition during these operations from
outside.

**Locally Cached Dependencies:** similarly to a standalone composition
construction, the components with dependencies will have a chance to cache their
dependencies when their `UpdateDependencies()` method gets called.

### Making Components Cloneable

To make a component cloneable:

- Override the `Clone()` method in your component class to return a new
  instance.
- Override `IsCloneable()` to return `true`.
- The default implementation throws if cloning is not supported.

**Example:**

```cpp
class MyComponent : public oxygen::Component {
  OXYGEN_COMPONENT(MyComponent)
public:
  std::unique_ptr<Component> Clone() const override {
    return std::make_unique<MyComponent>(*this);
  }
  bool IsCloneable() const noexcept override { return true; }
};
```

## Error Handling

The composition system throws `oxygen::ComponentError` exceptions for error
conditions such as:

- Adding a duplicate component
- Missing required dependencies
- Removing a component that is still required by another
- Failing to allocate a pooled component

Always catch `ComponentError` when performing operations that may fail, and
handle errors appropriately in your application logic.

## Diagnostics: Printing and Logging Components

You can inspect the state of a composition using the following methods:

- `PrintComponents(std::ostream&)`: Prints a summary of all components and their
  dependencies to the given output stream.
- `LogComponents()`: Logs a summary of all components using the engine's logging
  system.

These utilities are useful for debugging and verifying the state of your
compositions at runtime.

## Composition Capacity Tuning

When constructing a `Composition`, you can specify the initial capacity for
local and pooled components to optimize memory usage and performance:

```cpp
Composition my_comp(/*local_capacity=*/8, /*pooled_capacity=*/16);
```

This avoids unnecessary reallocations if you know the expected number of components in advance.

## Performance Benchmarks

| Benchmark                          | Time (ns) | Relative to PoolDirect | Relative to Fragmented | Notes                        |
|-------------------------------------|-----------|-----------------------|------------------------|------------------------------|
| RandomAccessLocalComponents         | 77,853    | 22.1x slower          | 18.4x slower           | Fastest random access        |
| RandomAccessPooledComponents        | 227,061   | 64.5x slower          | 53.8x slower           | Slowest random access        |
| RandomAccessHybridComponents        | 133,704   | 38.0x slower          | 31.7x slower           | Hybrid: in between           |
| SequentialAccessGetComponents       | 89,790    | 25.5x slower          | 21.3x slower           | Sequential, cache-friendly   |
| PoolDirectIteration                 | 3,519     | 1x (baseline)         | 0.83x (faster)         | Dense, contiguous pool       |
| FragmentedPoolDirectIteration       | 4,224     | 1.2x slower           | 1x (baseline)          | Fragmented pool              |

### Key Takeaways

- **Direct pool iteration** is the fastest access pattern by a wide margin, even with fragmentation.
- **Fragmentation** increases iteration time by about 20%, but the absolute cost is still very low.
- **Random access to pooled components** is the slowest pattern, due to indirection and poor cache locality.
- **Hybrid and local random access** are faster, but still much slower than direct pool iteration.
- **Sequential access** is always better than random for pooled/hybrid, but for local, the difference is small due to good cache locality.

### Summary

- For maximum performance, prefer direct pool iteration for pooled components.
- Fragmentation has a moderate but manageable impact on iteration speed.
- Use local (non-pooled) components for data that is not accessed in bulk or does not benefit from pooling.
- Use pooled components for high-frequency, bulk-accessed data to maximize cache locality and minimize access overhead.

For more details or to see the benchmark code, see `src/Oxygen/Composition/Benchmarks/composition_benchmark.cpp`.
