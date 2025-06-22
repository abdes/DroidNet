//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/ComponentPool.h>
#include <Oxygen/Composition/Detail/GetTrulySingleInstance.h>

namespace oxygen {

//=== ComponentPoolRegistry ===-----------------------------------------------//

/*!
 Global singleton registry for managing ComponentPool instances across modules.

 Uses GetTrulySingleInstance to ensure a single registry exists across all DLLs
 and modules in the process. Each component type gets its own pool instance,
 created on-demand with thread-safe lazy initialization.

 ### Key Features
 - **Cross-Module Safety**: Works reliably across DLL boundaries
 - **Thread Safety**: All operations are thread-safe with proper locking
 - **Lazy Initialization**: Pools are created only when first accessed
 - **Type Erasure**: Stores pools as void* with custom deleters for type safety
 - **Configurable Capacity**: Components can specify expected pool sizes

 ### Usage Examples
 ```cpp
 // Get a pool for a specific component type
 auto& pool = ComponentPoolRegistry::GetComponentPool<TransformComponent>();

 // Allocate a component
 auto handle = pool.Allocate(position, rotation);
 ```

 @note All component types must be registered in the global ResourceTypeList
 @warning Pools are never destroyed during program execution

 @see ComponentPool for individual pool operations
 @see GetTrulySingleInstance for cross-module singleton implementation
*/
class ComponentPoolRegistry {
public:
  //! Get the global singleton instance
  /*!
   Uses GetTrulySingleInstance to ensure a single registry across all modules.

   @return Reference to the global ComponentPoolRegistry instance

   @note Thread-safe operation
   */
  static auto Get() -> ComponentPoolRegistry&
  {
    return composition::detail::GetTrulySingleInstance<ComponentPoolRegistry>(
      "ComponentPoolRegistry");
  }
  //! Get the component pool for a specific pooled component type
  /*!
   Creates the pool on first access with thread-safe lazy initialization.

   @tparam PooledComponentType The component type (must be in ResourceTypeList)
   @return Reference to the ComponentPool for this type

   @note Thread-safe operation with exclusive locking during pool creation
   */
  template <typename PooledComponentType>
  static auto GetComponentPool() -> ComponentPool<PooledComponentType>&
  {
    return Get().GetPoolImpl<PooledComponentType>();
  }

  //! Clear all components from all pools
  /*!
   Removes all components from all existing pools, invalidating all handles.
   This is primarily intended for testing scenarios where clean state is needed
   between test cases.

   @note Thread-safe operation with exclusive locking
   @warning This invalidates all existing component handles across all pools
   */
  static void ClearAllPools() { Get().ClearAllPoolsImpl(); }

private:
  //! Internal pool access implementation
  /*!
   Thread-safe lazy initialization of component pools.

   @tparam PooledComponentType The component type to get pool for
   @return Reference to the ComponentPool for this type

   @note Uses exclusive locking during pool creation
   */
  template <typename PooledComponentType>
  auto GetPoolImpl() -> ComponentPool<PooledComponentType>&
  {
    std::lock_guard lock(pools_mutex_);
    auto resource_type = GetResourceTypeId<PooledComponentType,
      typename PooledComponentType::ResourceTypeList>();
    auto it = pools_.find(resource_type);

    if (it == pools_.end()) {
      auto pool = std::make_unique<ComponentPool<PooledComponentType>>(
        GetReserveCount<PooledComponentType>());
      auto* pool_ptr = pool.get();
      pools_.emplace(resource_type,
        PoolEntry(std::unique_ptr<void, void (*)(void*)>(pool.release(),
                    [](void* ptr) {
                      delete static_cast<ComponentPool<PooledComponentType>*>(
                        ptr);
                    }),
          [](void* ptr) {
            static_cast<ComponentPool<PooledComponentType>*>(ptr)->Clear();
          }));
      return *pool_ptr;
    }
    return *static_cast<ComponentPool<PooledComponentType>*>(
      it->second.pool_ptr.get());
  }
  //! Internal implementation for clearing all pools
  /*!
   Clears all components from all existing pools without destroying the pools.

   @note Uses exclusive locking to ensure thread safety
   */
  void ClearAllPoolsImpl()
  {
    std::lock_guard lock(pools_mutex_);
    for (auto& [type_id, pool_entry] : pools_) {
      // Call the stored clear function on the type-erased pool
      pool_entry.clear_fn(pool_entry.pool_ptr.get());
    }
  }

  //! Get the initial reserve count for a component type
  /*!
   Uses component's kExpectedPoolSize if available, otherwise default.

   @tparam PooledComponentType The component type
   @return Initial capacity for the pool

   @note Compile-time evaluation using C++20 concepts
   */
  template <typename PooledComponentType>
  static constexpr auto GetReserveCount() -> std::size_t
  {
    // Allow components to specify their expected pool size
    if constexpr (requires { PooledComponentType::kExpectedPoolSize; }) {
      return PooledComponentType::kExpectedPoolSize;
    }
    return 1024; // Default reserve count
  }
  std::mutex pools_mutex_; // Protects pools_ map during lazy initialization

  //! Storage for type-erased pools with their clear functions
  struct PoolEntry {
    std::unique_ptr<void, void (*)(void*)> pool_ptr;
    void (*clear_fn)(void*);

    PoolEntry(std::unique_ptr<void, void (*)(void*)> ptr, void (*clear)(void*))
      : pool_ptr(std::move(ptr))
      , clear_fn(clear)
    {
    }
  };

  std::unordered_map<ResourceHandle::ResourceTypeT, PoolEntry> pools_;
};

} // namespace oxygen
