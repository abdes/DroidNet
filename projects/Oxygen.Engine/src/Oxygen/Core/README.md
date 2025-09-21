# SafeCall Pattern

## Pattern Classification

- **Type**: Behavioral Pattern
- **Category**: Error Handling / Validation
- **Complexity**: Medium
- **Applicability**: High-performance systems requiring robust error handling

## Motivation

In complex systems like game engines, objects frequently transition between
valid and invalid states due to:

- Resource loading/unloading cycles
- Asynchronous operations
- Component lifecycle management
- Multi-threaded access patterns

Traditional approaches to validation often lead to:

- **Scattered validation logic** throughout the codebase
- **Inconsistent error handling** across different components
- **Performance overhead** from repeated validation checks
- **Silent failures** when validation is forgotten or bypassed

With SafeCall, complex code with many validation requirements and moving parts
becomes as simple as:

```cpp
ResourceManager rm;
if (auto handle = rm.LoadResource("texture.png")) {
    // Success - use handle.value()
} else {
    // Handle error gracefully
}
```

The implementation of `LoadResource` itself is a `SafeCall` that executes the
load operation, with a custom pre-requisites validator, and error handling.
Additional methods will also be implemented as as imple relay to a `SafeCall`.
See below...

## Intent

Provide a safe execution wrapper that validates object state before performing
operations, with automatic error handling and optional logging, while
maintaining performance in critical paths through optional unsafe variants.

The SafeCall pattern addresses validation challenges by:

- Centralizing validation logic within each class
- Providing consistent error handling and logging
- Enabling performance-critical paths through unsafe variants
- Guaranteeing exception safety with automatic error conversion
- Enforcing const correctness through separate mutable and const operation paths

## Structure

```text
┌─────────────────────────────────────────────────────┐
│                 SafeCall Pattern                    │
├─────────────────────────────────────────────────────┤
│                                                     │
│  Client                    SafeCall<T,V,F>          │
│  ┌─────────────┐          ┌─────────────────┐       │
│  │   calls     │ -------> │  validate()     │       │
│  │ SafeMethod  │          │  execute()      │       │
│  └─────────────┘          │  handleError()  │       │
│                           └─────────────────┘       │
│                                    |                │
│                                    v                │
│  Target Object              Optional Logger         │
│  ┌─────────────────┐      ┌─────────────────┐       │
│  │  state_valid    │      │ LogSafeCallError│       │
│  │  Validate()     │      │     (concept)   │       │
│  │  Operation()    │      └─────────────────┘       │
│  └─────────────────┘                                │
└─────────────────────────────────────────────────────┘
```

## Participants

### SafeCall Template Function

- Core template function providing validated execution
- Handles const/non-const operations automatically
- Provides exception safety guarantees
- Returns `std::optional<T>` for type-safe error handling

### Target Object

- Object whose operations are being validated and executed
- Implements validation logic appropriate to its state
- Optionally implements logging interface for error reporting
- Provides both safe and unsafe operation variants

### Validator

- Callable (lambda, function pointer, or member function) that validates object state
- Returns std::optional<std::string> (nullopt = valid, string = error message)
- Can be customized per operation or shared across operations

### Optional Logger (Concept-based)

- Detected via HasLogSafeCallError concept
- Provides LogSafeCallError(const char*) method
- Enables automatic error logging without tight coupling

## Key Features

### Core Template Function

```cpp
template <typename TargetRef, typename Validator, typename Func>
    requires std::invocable<Func, TargetRef> && std::invocable<Validator, TargetRef>
auto SafeCall(TargetRef&& target, Validator&& validate, Func&& func) noexcept;
```

[_Full implementation on GitHub_](https://github.com/abdes/DroidNet/blob/master/projects/Oxygen.Engine/src/Oxygen/Core/SafeCall.h)

Supports any callable, including `std::function`, lambda, methods, frerform
functions, etc.

Supports sophisticated const correctness through dual State structs and
template-based validation kept outside the class declaration to avoid code
duplication.

### Concept-Based Logging Detection

```cpp
template <typename T>
concept HasLogSafeCallError = requires(T t, const char* msg) {
    { t.LogSafeCallError(msg) } -> std::same_as<void>;
};
```

Classes automatically get logging support by implementing the LogSafeCallError
method, with no inheritance or interface requirements.

### Exception Safety

All exceptions from operation functions are caught and converted to std::nullopt
returns, ensuring noexcept behavior for the SafeCall wrapper.

### Flexible Validation

Validation can be implemented as:

- **Lambda functions** for inline validation logic
- **Member function pointers** for reusable validation methods
- **External function pointers** for shared validation across classes

### CRTP Support for Inheritance

In a class hierarchy, often it becomes complicated to provide the proper target
type type to the `SafeCall` method. But CRTP can help...

```cpp
template <typename Derived>
struct SafeBase {
    template <typename Func>
    auto SafeCall(Func&& func) {
        return oxygen::SafeCall(
            *static_cast<Derived*>(this),
            &Derived::Validate,
            std::forward<Func>(func)
        );
    }
};
```

## Implementation Example

This example demonstrates the SafeCall pattern with const correctness,
safe/unsafe variants, concept-based logging, and template validation hidden in
the implementation file:

**ResourceManager.h:**

```cpp
#pragma once
#include <vector>
#include <memory>
#include <optional>
#include <string>
#include "Oxygen/Core/SafeCall.h"

class ResourceManager {
private:
    std::vector<Resource> resource_pool_;
    std::unique_ptr<Allocator> allocator_;
    bool initialized_{false};
    size_t max_resources_{1000};

    // Dual State structs for const/mutable access
    struct State {
        ResourceManager* manager = nullptr;
        std::vector<Resource>* resource_pool = nullptr;
        Allocator* allocator = nullptr;
    };

    struct ConstState {
        const ResourceManager* manager = nullptr;
        const std::vector<Resource>* resource_pool = nullptr;
        const Allocator* allocator = nullptr;
    };

    // Validation methods (implemented in .cpp to hide template details).
    // Support mutable and immutable operations.
    auto ValidateForSafeCall(State& state) const noexcept -> std::optional<std::string>;
    auto ValidateForSafeCall(ConstState& state) const noexcept -> std::optional<std::string>;

    // Concept-based logging support (automatically detected)
    void LogSafeCallError(const char* reason) const noexcept;

    // SafeCall wrapper methods (const and non-const)
    template <typename Func>
    auto SafeCall(Func&& func) const noexcept {
        ConstState state;
        return oxygen::SafeCall(
            *this,
            [this, &state](const ResourceManager&) { return ValidateForSafeCall(state); },
            [func = std::forward<Func>(func), &state](const ResourceManager&) mutable {
                return func(state);
            });
    }

    template <typename Func>
    auto SafeCall(Func&& func) noexcept {
        State state;
        return oxygen::SafeCall(
            *this,
            [this, &state](ResourceManager&) { return ValidateForSafeCall(state); },
            [func = std::forward<Func>(func), &state](ResourceManager&) mutable {
                return func(state);
            });
    }

public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    // Unsafe methods for performance-critical paths
    auto LoadResourceUnchecked(const std::string& path) -> ResourceHandle;
    auto GetResourceUnchecked(ResourceHandle handle) const -> const Resource&;

    // Safe methods using SafeCall pattern with automatic const correctness
    auto LoadResource(const std::string& path) noexcept -> std::optional<ResourceHandle> {
        return SafeCall([&](const State& state) {
            auto& resource = state.resource_pool->emplace_back(path);
            state.allocator->RegisterResource(resource);
            return resource.GetHandle();
        });
    }

    auto GetResource(ResourceHandle handle) const noexcept -> std::optional<const Resource*> {
        return SafeCall([handle](const ConstState& state) -> const Resource* {
            if (handle.index >= state.resource_pool->size()) return nullptr;
            return &(*state.resource_pool)[handle.index];
        });
    }

    auto GetResourceCount() const noexcept -> std::optional<size_t> {
        return SafeCall([](const ConstState& state) {
            return state.resource_pool->size();
        });
    }

    auto UnloadResource(ResourceHandle handle) noexcept -> std::optional<bool> {
        return SafeCall([handle](const State& state) -> bool {
            if (handle.index >= state.resource_pool->size()) return false;
            state.allocator->UnregisterResource((*state.resource_pool)[handle.index]);
            state.resource_pool->erase(state.resource_pool->begin() + handle.index);
            return true;
        });
    }

private:
    // Utility methods
    bool IsInitialized() const noexcept { return initialized_; }
    void Initialize() noexcept { initialized_ = true; }
    size_t GetMaxResources() const noexcept { return max_resources_; }
};
```

**ResourceManager.cpp:**

```cpp
#include "ResourceManager.h"
#include "Resource.h"
#include "Allocator.h"
#include "Logger.h"

namespace {
// Template validation function that works with both State types
// Hidden in implementation file to avoid template dependencies in header
template <typename StateT>
auto ValidateAndPopulateResourceState(ResourceManager* mgr, StateT& state) noexcept
    -> std::optional<std::string> {
    state.manager = mgr;

    if (!state.manager->IsInitialized()) {
        return "ResourceManager not initialized";
    }

    if (state.manager->GetResourceCount() >= state.manager->GetMaxResources()) {
        return "Resource limit exceeded";
    }

    // Populate state with validated pointers
    state.resource_pool = &const_cast<std::vector<Resource>&>(
        const_cast<const ResourceManager*>(mgr)->GetResourcePool());
    state.allocator = &const_cast<Allocator&>(
        const_cast<const ResourceManager*>(mgr)->GetAllocator());

    return std::nullopt; // Valid state
}
}

// Thin wrapper implementations that delegate to the common template
auto ResourceManager::ValidateForSafeCall(State& state) const noexcept
    -> std::optional<std::string> {
    return ValidateAndPopulateResourceState(const_cast<ResourceManager*>(this), state);
}

auto ResourceManager::ValidateForSafeCall(ConstState& state) const noexcept
    -> std::optional<std::string> {
    return ValidateAndPopulateResourceState(const_cast<ResourceManager*>(this), state);
}

// Unsafe method implementations
auto ResourceManager::LoadResourceUnchecked(const std::string& path) -> ResourceHandle {
    auto& resource = resource_pool_.emplace_back(path);
    allocator_->RegisterResource(resource);
    return resource.GetHandle();
}

auto ResourceManager::GetResourceUnchecked(ResourceHandle handle) const -> const Resource& {
    return resource_pool_[handle.index];
}

// Concept-based logging implementation
void ResourceManager::LogSafeCallError(const char* reason) const noexcept {
    try {
        Logger::Error("ResourceManager operation failed: {}", reason);
    } catch (...) {
        // If logging fails, we can't do anything about it in a noexcept context
    }
}

// Private utility method implementations
const std::vector<Resource>& ResourceManager::GetResourcePool() const noexcept {
    return resource_pool_;
}

const Allocator& ResourceManager::GetAllocator() const noexcept {
    return *allocator_;
}
```

## Applicability

Use the SafeCall pattern when:

- **Object state validation** is critical for operation safety
- **Consistent error handling** is needed across multiple operations
- **Optional logging** should be available without tight coupling
- **Performance-critical paths** need unsafe variants for optimization
- **Exception safety** is required with automatic error conversion
- **Complex validation logic** would otherwise be scattered throughout code

## Consequences

**Benefits:**

- **Centralized validation logic** - Each class controls its own validation rules
- **Consistent error handling** - Uniform `std::optional<T>` return pattern
- **Exception safety** - Automatic exception catching and conversion
- **Optional logging** - Concept-based detection without inheritance requirements
- **Performance flexibility** - Safe and unsafe variants available
- **Type safety** - Template-based with compile-time validation

**Drawbacks:**

- **Template complexity** - Advanced C++ template techniques required
- **Compilation overhead** - Template instantiation for each usage
- **Learning curve** - Developers must understand the wrapper pattern
- **Code duplication** - Each class needs SafeCall wrapper implementation

## Implementation Considerations

### Performance Characteristics

- **Safe path**: Validation + operation + error handling overhead
- **Unsafe path**: Direct operation call with no overhead
- **Template inlining**: Most overhead eliminated in optimized builds

### Memory Footprint

- **Zero runtime overhead** for logging detection (concept-based)
- **Minimal memory usage** - `std::optional<T>` return values only
- **No vtable overhead** - Template-based, not inheritance-based

### Thread Safety

- **No inherent thread safety** - validation and operations must handle concurrency
- **Exception safety guaranteed** - All exceptions caught and converted
- **Atomic operations supported** - Can be used with atomic state variables

## Related Patterns

### Command Pattern

- SafeCall can wrap Command execution for validated command processing
- Provides error handling for command validation and execution

### Template Method Pattern

- Validation step is customizable while execution flow is fixed
- Different classes can provide different validation algorithms

### Decorator Pattern

- SafeCall decorates operations with validation and error handling
- Can be composed with other decorators for additional functionality

### RAII (Resource Acquisition Is Initialization)

- Often used together for resource management with state validation
- SafeCall ensures resources are only used when in valid states
