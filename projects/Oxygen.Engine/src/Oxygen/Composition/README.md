# Oxygen Engine Composition System

# Implementation Plan

| Done | Description |
|:----:|-------------|
| ✅ | **COMPLETED**: Implement template metaprogramming resource type system using TypeList and IndexOf |
| ✅ | **COMPLETED**: Update Resource.h with compile-time type ID generation system |
| ✅ | **COMPLETED**: Design centralized ResourceTypeList for consistent type ID allocation |
| ✅ | **COMPLETED**: Create comprehensive test suite for Resource template metaprogramming system |
| ✅ | **COMPLETED**: Implement thread-safe ComponentPool template with compile-time type resolution and contiguous storage |
| ✅ | **COMPLETED**: Implement ComponentPoolRegistry class that manages ComponentPool instances using GetTrulySingleInstance |
| ✅ | **COMPLETED**: Add ComponentPoolRegistry initialization function to cs_init.cpp for global singleton system |
| ✅ | **COMPLETED**: Refactor `Composition` for hybrid storage: add dual containers (unique and pooled) |
| ✅ | **COMPLETED**: Implement compile-time storage selection: Update `AddComponent`, `GetComponent`, `RemoveComponent`, and related APIs to use `if constexpr` and type traits to select the correct storage. |
| ✅ | **COMPLETED**: Update component declaration macros: Ensure `OXYGEN_COMPONENT` and (if needed) `OXYGEN_RESOURCE_COMPONENT` macros correctly detect and register pooled vs. non-pooled components. |
| ✅ | **COMPLETED**: Integrate pooled component access: Route pooled component operations through `ComponentPoolRegistry` and ensure handle validation and error handling are robust. |
| ✅ | **COMPLETED**: Implement and test dependency-aware removal: Prevent removal of components that are required by others, and ensure dependency resolution works for both storage types. |
| ❌ | Write and run unit tests for hybrid storage: Cover all scenarios—adding, removing, accessing, and dependency management for both pooled and non-pooled components. |
| ❌ | Profile component usage: Identify which components are performance-critical and should be pooled, using real-world or synthetic benchmarks. |
| ❌ | Benchmark pooled vs. non-pooled performance: Measure and compare access times, memory usage, and cache locality for both storage types. |
| ❌ | Optimize pool sizing and growth: Tune initial pool sizes and growth strategies based on profiling data; document best practices. |
| ❌ | Improve handle and pointer validation: Enhance cached pointer validation and handle-based access performance, especially for pooled components. |
| ❌ | Expand pooling to additional components: Based on profiling, migrate more components to pooled storage as appropriate. |
| ❌ | Document usage guidelines and performance recommendations: Provide clear documentation for engine users on when and how to use pooled vs. non-pooled components, and how to declare new components. |

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
  `RenderThread` (non pooled)

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
2. **MSVC Compatibility**: No reliance on compiler-specific macros like `__COUNTER__`
3. **No RTTI Dependency**: Works in environments where RTTI is disabled
4. **Cross-Module Consistency**: Type IDs are consistent across DLL boundaries

ComponentPoolRegistry provides a truly global singleton for managing component
pools across all modules:

```cpp
//=== Component Pool Architecture ===----------------------------------------//

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Composition/Detail/GetTrulySingleInstance.h>
#include <Oxygen/Composition/ResourceTypes.h>  // Global resource type registry
#include <shared_mutex>

//=== Compile-Time Resource Type System ===----------------------------------//

/*!
 Template metaprogramming-based resource type system.
 Provides compile-time unique type IDs without RTTI or runtime overhead.
*/
template <typename... Ts>
struct TypeList {};

/*!
 Helper to get the index of a type in a TypeList.
 Returns compile-time constant index for O(1) type resolution.
*/
template <typename T, typename List>
struct IndexOf;

template <typename T, typename... Ts>
struct IndexOf<T, TypeList<T, Ts...>> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename U, typename... Ts>
struct IndexOf<T, TypeList<U, Ts...>> : std::integral_constant<std::size_t, 1 + IndexOf<T, TypeList<Ts...>>::value> {};

/*!
 Get compile-time unique resource type ID for any registered type.
 Zero runtime overhead - all type resolution happens at compile time.

 @tparam T The resource type to get ID for
 @tparam ResourceTypeList The centralized TypeList containing all valid resource types
 @return Compile-time constant resource type ID
*/
template <typename T, typename ResourceTypeList>
constexpr auto GetResourceTypeId() noexcept -> ResourceHandle::ResourceTypeT {
    static_assert(IndexOf<T, ResourceTypeList>::value < 256,
                  "Too many resource types for ResourceHandle::ResourceTypeT!");
    return static_cast<ResourceHandle::ResourceTypeT>(IndexOf<T, ResourceTypeList>::value);
}
```

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
a new `ComponentPool` instance with appropriate initial capacity. The pool is
stored in a type-erased container with custom deleter functions to ensure proper
cleanup.

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

## Composition with Hybrid Component Storage

The Composition class supports hybrid component storage, automatically routing components to appropriate storage based on their inheritance hierarchy. This provides transparent performance optimization for high-frequency components while maintaining simplicity for occasional-use components.

### **Storage Architecture**

**Dual Container System**: The `ComponentManager` maintains separate storage for different component types:

```cpp
struct Composition::ComponentManager {
    // Existing non-pooled storage
    ComponentsCollection components_;
    std::unordered_map<TypeId, size_t> component_index_;

    // New pooled component storage
    std::unordered_map<TypeId, ResourceHandle> pooled_components_;

    // Thread safety for read-heavy workload
    mutable std::shared_mutex access_mutex_;
};
```

**Automatic Storage Selection**: Components are automatically routed to appropriate storage based on inheritance:
- **Pooled Components**: Inherit from both `Component` and `Resource<T, ResourceTypeList>` → stored in global `ComponentPool`
- **Non-Pooled Components**: Inherit only from `Component` → stored directly in composition

### **Component Declaration Macros**

**Single Macro with Automatic Detection**: The `OXYGEN_COMPONENT` macro automatically detects Resource inheritance and generates appropriate integration code:

```cpp
// Non-pooled component (simple inheritance)
class SettingsComponent : public oxygen::Component {
    OXYGEN_COMPONENT(SettingsComponent)
public:
    explicit SettingsComponent(const std::string& config) : config_(config) {}

private:
    std::string config_;
};

// Pooled component (dual inheritance)
class TransformComponent : public oxygen::Component,
                          public oxygen::Resource<TransformComponent, ResourceTypeList> {
    OXYGEN_COMPONENT(TransformComponent)
public:
    explicit TransformComponent(const Vector3& position) : position_(position) {}

    auto GetHandle() const -> ResourceHandle { return Resource::GetHandle(); }

private:
    Vector3 position_;
};
```

### 4. Component Declaration Macros

 - `OXYGEN_COMPONENT(T)` works for both pooled and non-pooled components.
 - No fallback macro is required: with the `PooledComponent` concept, `OXYGEN_COMPONENT` is sufficient for all component types. Only use a fallback macro (e.g., `OXYGEN_RESOURCE_COMPONENT`) if you encounter a rare case where automatic detection fails due to unusual inheritance or template instantiation issues.

### **Unified Component Access**

**Transparent API**: All component access uses the same interface regardless of storage type:

```cpp
// Template metaprogramming routes to appropriate storage
template<typename T>
auto Composition::GetComponent() -> T& {
    std::shared_lock lock(pimpl_->access_mutex_);

    if constexpr (std::is_base_of_v<Resource<T, ResourceTypeList>, T>) {
        // Pooled component - access via ComponentPoolRegistry
        auto& registry = ComponentPoolRegistry::GetInstance();
        auto& pool = registry.GetPool<T>();
        auto handle = pimpl_->pooled_components_.at(T::ClassTypeId());
        return pool.Get(handle);  // Throws if handle invalid
    } else {
        // Non-pooled component - existing direct access
        return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
    }
}
```

**Consistent Error Handling**: Both storage types throw `ComponentError` for missing or invalid components, maintaining existing behavior patterns.

### **Smart Dependency Resolution**

**Automatic Dependency Storage**: Components use `if constexpr` to automatically adapt to dependency storage types:

```cpp
class RenderComponent : public oxygen::Component {
    OXYGEN_COMPONENT(RenderComponent)
    OXYGEN_COMPONENT_REQUIRES(TransformComponent, MaterialComponent)

public:
    void UpdateDependencies(const oxygen::Composition& composition) override {
        // Automatically detect and store appropriate reference type
        auto& transform = composition.GetComponent<TransformComponent>();
        auto& material = composition.GetComponent<MaterialComponent>();

        if constexpr (requires { transform.GetHandle(); }) {
            transform_handle_ = transform.GetHandle();  // Store handle for pooled
        } else {
            transform_ptr_ = &transform;                // Store pointer for non-pooled
        }

        if constexpr (requires { material.GetHandle(); }) {
            material_handle_ = material.GetHandle();
        } else {
            material_ptr_ = &material;
        }
    }

private:
    // Dependency storage adapts to component type
    std::optional<ResourceHandle> transform_handle_;
    TransformComponent* transform_ptr_{nullptr};

    std::optional<ResourceHandle> material_handle_;
    MaterialComponent* material_ptr_{nullptr};
};
```

### **Thread Safety Model**

**Read-Heavy Optimization**: The system is optimized for frequent `GetComponent()` calls with rare composition modifications:

- **Shared Locks**: Multiple threads can access components simultaneously
- **Exclusive Locks**: Component addition/removal requires exclusive access
- **Pool Thread Safety**: `ComponentPool` provides its own thread-safe operations
- **Handle Validation**: Stale handle detection works across thread boundaries

### **Component Lifecycle Management**

**Dependency-Aware Removal**: Components cannot be removed if other components depend on them (existing `IsComponentRequired()` validation):

```cpp
// Existing validation prevents removal of required components
if (IsComponentRequired(component_id)) {
    throw ComponentError("Cannot remove component: other components depend on it");
}
```

**Handle Invalidation**: When pooled components are removed, existing handles become invalid but are not actively invalidated (standard game engine behavior). Components detect stale handles via nullptr returns during access.

### **Enhanced Debug Output**

**Storage Type Indication**: The `PrintComponents()` method distinguishes between storage types and provides developer-focused information:

```cpp
void Composition::PrintComponents(std::ostream& out) const {
    // ...existing object name logic...

    // Print non-pooled components
    for (const auto& entry : pimpl_->components_) {
        out << "   [" << entry->GetTypeId() << "] " << entry->GetTypeName()
            << " (Direct)";
        PrintDependencyInfo(out, *entry);
        out << "\n";
    }

    // Print pooled components with handle information
    for (const auto& [type_id, handle] : pimpl_->pooled_components_) {
        auto& registry = ComponentPoolRegistry::GetInstance();
        auto* component = registry.GetComponentByHandle(handle);
        if (component) {
            out << "   [" << type_id << "] " << component->GetTypeName()
                << " (Pooled, Handle: " << handle.GetIndex()
                << ", Gen: " << handle.GetGeneration() << ")";
            PrintDependencyInfo(out, *component);
        } else {
            out << "   [" << type_id << "] <Invalid Handle: "
                << handle.GetIndex() << ">";
        }
        out << "\n";
    }
}
```

**Dependency Resolution Details**: Debug output indicates whether dependencies are accessed via handles or pointers, helping developers understand the performance characteristics of their composition structure.

### **Performance Benefits**

**Optimized Access Patterns**:
- **Pooled Components**: O(1) access with handle validation, cache-friendly memory layout
- **Non-Pooled Components**: Direct pointer access, minimal overhead
- **Thread Safety**: Reader-writer locks favor the common read-heavy access pattern
- **Zero Runtime Overhead**: Storage routing determined at compile-time via template metaprogramming

**Memory Efficiency**:
- **Pooled Storage**: Contiguous memory layout improves cache locality for frequently accessed components
- **Direct Storage**: Minimal overhead for components that don't benefit from pooling
- **Handle Validation**: Automatic detection of stale references prevents memory corruption bugs

## Writing Components

Components are the building blocks of the Oxygen composition system. They
encapsulate specific functionality that can be combined to create complex game
objects. Writing a component involves inheriting from the `oxygen::Component`
base class and following a few key patterns to integrate with the type system
and composition framework.

Every component should:
- Inherit from `oxygen::Component`
- Use the `OXYGEN_COMPONENT` macro for proper integration
- Have a protected constructor (enforced by the macro)
- Be instantiated only through the `Composition::AddComponent<T>()` method
- Optionally declare dependencies on other components

Components come in two flavors: **non-pooled** (stored directly in compositions)
and **pooled** (stored in global ResourceTable for high-frequency access). Most
components are non-pooled unless they require the performance benefits of
contiguous memory layout.

### OXYGEN_COMPONENT Macro

The `OXYGEN_COMPONENT` macro is the standard way to declare a component class.
It combines type system registration with component-specific functionality:

```cpp
class MyComponent : public oxygen::Component {
    OXYGEN_COMPONENT(MyComponent)  // Generates type info + component integration
public:
    explicit MyComponent(int value) : value_(value) {}

    int GetValue() const { return value_; }

private:
    int value_;
};
```

What `OXYGEN_COMPONENT(MyComponent)` generates:

1. **Type System Integration**: Calls `OXYGEN_TYPED(MyComponent)` to generate:
   - `static constexpr auto ClassTypeName()` - Returns compiler-specific type name string
   - `static auto ClassTypeId()` - Returns unique TypeId from global TypeRegistry
   - `auto GetTypeId() const override` - Instance method returning ClassTypeId()
   - `auto GetTypeName() const override` - Instance method returning ClassTypeName()

2. **Component-Specific Features**:
   - `friend class oxygen::Composition` - Allows Composition to access protected constructor
   - Protected section placement - Ensures components can only be instantiated by Composition

### Component Dependencies

Components can declare dependencies on other components using `OXYGEN_COMPONENT_REQUIRES`:

```cpp
class DependentComponent : public oxygen::Component {
    OXYGEN_COMPONENT(DependentComponent)
    OXYGEN_COMPONENT_REQUIRES(FirstDependency, SecondDependency)
public:
    void UpdateDependencies(const oxygen::Composition& composition) override {
        first_ = &composition.GetComponent<FirstDependency>();
        second_ = &composition.GetComponent<SecondDependency>();
    }

private:
    FirstDependency* first_{nullptr};
    SecondDependency* second_{nullptr};
};
```

The dependency system ensures:
- Dependencies are created before dependent components
- Dependencies are destroyed after dependent components
- `UpdateDependencies()` is called after all dependencies exist
- Circular dependencies are detected at runtime

For performance-critical scenarios involving ResourceTable storage, a separate
resource type system operates alongside the general type registry. This
specialized system assigns compile-time resource type identifiers to classes
that inherit from `Resource<T, ResourceTypeList>`. Unlike the general type
system, resource types face a practical constraint of 256 maximum types due to
the underlying ResourceHandle structure, but they benefit from
zero-runtime-overhead type resolution and optimal memory layout for
high-frequency operations.

# Hybrid Storage Architecture for Composition

The hybrid storage system in Oxygen Engine's Composition class enables
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

### 2. ComponentManager Storage Layout

- Dual containers:
  - `std::unordered_map<TypeId, ResourceHandle> pooled_components_` for pooled.
  - `ComponentsCollection components_` and `std::unordered_map<TypeId, size_t>
    component_index_` for unique.
- All access is protected by a `mutable std::shared_mutex mutex_` in the
  `Composition` class (not in `ComponentManager`).

### 3. API Routing Using Concepts

- All major APIs (`AddComponent`, `GetComponent`, `RemoveComponent`) use `if
  constexpr (PooledComponent<T>)` to select the correct storage path.
- Locking is performed in the `Composition` class before any routing or dispatch
  to `ComponentManager`.
- Example for `GetComponent`:

  ```cpp
  template <typename T>
  auto Composition::GetComponent() -> T& {
      std::shared_lock lock(mutex_);
      if constexpr (PooledComponent<T>) {
          auto handle = pimpl_->pooled_components_.at(T::ClassTypeId());
          auto& pool = ComponentPoolRegistry::GetInstance().GetPool<T>();
          return pool.Get(handle); // Throws if handle invalid
      } else {
          return static_cast<T&>(GetComponentImpl(T::ClassTypeId()));
      }
  }
  ```

  - `AddComponent` and `RemoveComponent` follow the same pattern, routing to the
    pool or direct storage as appropriate, with locking always performed in
    `Composition`.

### 4. Component Declaration Macros

- `OXYGEN_COMPONENT(T)` works for both pooled and non-pooled components.
- No fallback macro is required: with the `PooledComponent` concept,
  `OXYGEN_COMPONENT` is sufficient for all component types. Only use a fallback
  macro (e.g., `OXYGEN_RESOURCE_COMPONENT`) if you encounter a rare case where
  automatic detection fails due to unusual inheritance or template instantiation
  issues.

### 5. Dependency Resolution

- Use `if constexpr (PooledComponent<DependencyType>)` to store either a
  handle (for pooled) or pointer (for unique) to dependencies.
  Example:
  ```cpp
  if constexpr (PooledComponent<TransformComponent>) {
      transform_handle_ = transform.GetHandle();
  } else {
      transform_ptr_ = &transform;
  }
  ```

### 6. Error Handling and Diagnostics

- If a type is not in the resource list but is used as pooled, a static_assert
  will fail at compile time.
- If a handle is invalid or missing, `ComponentError` is thrown at runtime.
- Concepts provide clear diagnostics if a type does not meet pooled requirements.

### 7. Extensibility and Maintenance

- New component types require no API changes—just inherit from the correct base
  and use the macro.
- If the pooled trait changes, only the concept definition needs updating.

### 8. Summary Table

| Component Type   | Concept/Inheritance Pattern                      | Storage Location         | Access Pattern         |
|------------------|--------------------------------------------------|-------------------------|-----------------------|
| Pooled           | `PooledComponent<T>` (inherits Resource<...>)    | Global ComponentPool    | Handle via pool       |
| Unique/Non-pooled| Not `PooledComponent<T>` (inherits Component)    | Per-composition         | Direct pointer        |

### 9. Example: Macro Usage

```cpp
// Pooled component
class TransformComponent
  : public oxygen::Component
  , public oxygen::Resource<TransformComponent, ResourceTypeList> {
    OXYGEN_COMPONENT(TransformComponent)
    // ...
};
// Unique component
class SettingsComponent : public oxygen::Component {
    OXYGEN_COMPONENT(SettingsComponent)
    // ...
};
```

### 10. Example: Dependency-Aware Removal

- Before removing a component, check if it is required by others (dependency graph).
- If required, throw `ComponentError`.

### 11. Benefits

- Zero runtime overhead for storage selection.
- Unified, expressive API for all component types.
- Improved compile-time safety and diagnostics via concepts.
- Extensible and maintainable.
