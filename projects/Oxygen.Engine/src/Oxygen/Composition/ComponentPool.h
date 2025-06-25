//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <shared_mutex>
#include <span>
#include <utility>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Detail/ComponentPoolUntyped.h>

namespace oxygen {

//=== ComponentPool ===-------------------------------------------------------//

/*!
 Thread-safe pool for high-frequency pooled components using ResourceTable.

 Provides O(1) allocation, deallocation, and access with automatic handle
 validation and built-in defragmentation support. Uses compile-time template
 metaprogramming for zero-runtime-overhead type resolution.

 @tparam PooledComponentType The component type to pool (must be in
 ResourceTypeList)

 ### Performance Characteristics
 - Time Complexity: O(1) for all operations
 - Memory: Contiguous storage with sparse/dense optimization
 - Optimization: Built-in defragmentation and cache locality

 ### Usage Examples
 ```cpp
 // Create pool for a pooled component type
 ComponentPool<TransformComponent> pool(2048);

 // Allocate component
 auto handle = pool.Allocate(position, rotation);

 // Access component
 if (auto* transform = pool.Get(handle)) {
     transform->SetPosition(new_pos);
 }

 // Deallocate when done
 pool.Deallocate(handle);
 ```

 @warning All operations are thread-safe but references will become invalid
          during table reallocation. Always re-acquire references after
          operations that might cause growth.

 @see ResourceTable for underlying sparse/dense storage implementation
 @see ComponentPoolRegistry for global pool management
*/
template <IsPooledComponent PooledComponentType>
class ComponentPool : public composition::detail::ComponentPoolUntyped {
public:
  using Handle = ResourceHandle;

  /*!
   Construct component pool with specified initial capacity.

   @param reserve_count Initial capacity for the pool
   */
  explicit ComponentPool(std::size_t reserve_count = 1024)
    : table_(PooledComponentType::GetResourceType(), reserve_count)
  {
  }

  //! Thread-safe component allocation
  /*!
   Creates a new component instance in the pool.

   @param args Constructor arguments for the component
   @return Handle to the allocated component

   @note Thread-safe operation with exclusive locking
   */
  template <typename... Args> auto Allocate(Args&&... args) -> Handle
  {
    std::lock_guard lock(mutex_);
    return table_.Emplace(std::forward<Args>(args)...);
  }

  //! Allocate a new component, using the provided \p comp as value.
  /*!
   Allocates a new component in the pool by copying the given source component.
   If the type does not match, throws.
   @param comp The component to value. Will be moved into the pool table.
   @return Handle to the new component.
  */
  auto Allocate(Component&& comp) // NOLINT(*-rvalue-reference-param-not-moved)
    -> ResourceHandle override
  {
    // Attempt to dynamic_cast to the correct type
    DCHECK_F(comp.GetTypeId() == PooledComponentType::ClassTypeId(),
      "ComponentPool::Allocate: type mismatch, expected {}, got {}",
      PooledComponentType::ClassTypeId(), comp.GetTypeId());
    auto* typed = static_cast<PooledComponentType*>(&comp);
    if (!typed) {
      throw std::invalid_argument("ComponentPool::Allocate: type mismatch");
    }
    std::lock_guard lock(mutex_);
    return table_.Insert(std::move(*typed));
  }

  //! Thread-safe component deallocation
  /*!
   Removes component from the pool and invalidates the handle.

   @param handle Handle to the component to deallocate

   @note Thread-safe operation with exclusive locking
   */
  auto Deallocate(Handle handle) noexcept -> size_t override
  {
    std::lock_guard lock(mutex_);
    try {
      auto erased = table_.Erase(handle);
      if (erased != 1) {
        LOG_F(WARNING, "Component({}) not removed from table",
          oxygen::to_string_compact(handle));
      }
      return erased;
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "exception when deallocating component({}): {}",
        oxygen::to_string_compact(handle), ex.what());
    } catch (...) {
      // Any exception is non-recoverable, log and return 0
      LOG_F(ERROR, "unknown exception when deallocating component({})",
        oxygen::to_string_compact(handle));
    }
    return 0;
  }

  //! Thread-safe component access - returns nullptr if handle is invalid
  /*!
   @param handle Handle to the component
   @return Pointer to component or nullptr if handle is invalid

   @note Thread-safe operation with shared locking
   */
  auto Get(Handle handle) noexcept -> PooledComponentType*
  {
    return const_cast<PooledComponentType*>(std::as_const(*this).Get(handle));
  }

  //! Thread-safe const component access
  /*!
   @param handle Handle to the component
   @return Const pointer to component or nullptr if handle is invalid

   @note Thread-safe operation with shared locking
   */
  auto Get(Handle handle) const noexcept -> const PooledComponentType*
  {
    std::shared_lock lock(mutex_);
    try {
      return &table_.ItemAt(handle);
    } catch (const std::out_of_range&) {
      return nullptr;
    } catch (...) {
      // Any other exception is unexpected, return nullptr
      LOG_F(ERROR,
        "Unexpected exception when getting a component for handle: {}",
        oxygen::to_string_compact(handle));
      return nullptr;
    }
  }

  //! Leverage ResourceTable's defragmentation with thread safety
  /*!
   @param comp Comparison function for ordering during defragmentation
   @param max_swaps Maximum number of swaps to perform (0 = unlimited)
   @return Number of swaps performed

   @note Thread-safe operation with exclusive locking
   */
  template <typename Compare>
  auto Defragment(Compare comp, std::size_t max_swaps = 0) -> std::size_t
  {
    std::lock_guard lock(mutex_);
    return table_.Defragment(comp, max_swaps);
  }

  //! Defragment using component's default ordering (if available)
  /*!
   Uses component's static Compare method if available, otherwise no-op.

   @param max_swaps Maximum number of swaps to perform (0 = unlimited)
   @return Number of swaps performed

   @note Thread-safe operation with exclusive locking
   */
  auto Defragment(std::size_t max_swaps = 0) -> std::size_t
  {
    if constexpr (requires(const PooledComponentType& a,
                    const PooledComponentType& b) {
                    PooledComponentType::Compare(a, b);
                  }) {
      return Defragment(
        [](const PooledComponentType& a, const PooledComponentType& b) {
          return PooledComponentType::Compare(a, b);
        },
        max_swaps);
    } else {
      // No-op if no comparison available
      return 0;
    }
  }

  //! Thread-safe access to dense component array
  /*!
   @return Read-only span of all components in dense storage order

   @note Thread-safe operation with shared locking
   @warning Span becomes invalid when lock is released
   */
  auto GetDenseComponents() const -> std::span<const PooledComponentType>
  {
    std::shared_lock lock(mutex_);
    return table_.Items();
  }

  //! Thread-safe size queries
  /*!
   @return Number of components currently in the pool

   @note Thread-safe operation with shared locking
   */
  auto Size() const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    return table_.Size();
  }

  //! Check if pool is empty
  /*!
   @return true if pool contains no components

   @note Thread-safe operation with shared locking
   */
  auto IsEmpty() const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    return table_.IsEmpty();
  }
  //! Get the resource type for this component pool
  /*!
   @return Resource type ID for this component type

   @note No locking required - resource type is immutable
   */
  auto GetComponentType() const noexcept -> ResourceHandle::ResourceTypeT
  {
    return table_.GetItemType();
  }

  //! Clear all components from the pool, invalidating all handles.
  /*!
   Under normal circumstances, the lifecycle of components in the pool is
   managed by the owning compositions. There is no need to call this method.
   However, when recovering from critical errors, it may be necessary to put the
   global pools in a clean state. This is the only reasonable use case for
   calling this method.

   @note Thread-safe operation with exclusive locking
   */
  auto ForceClear() noexcept -> void
  {
    std::lock_guard lock(mutex_);
    table_.Clear();
  }

  // Type-erased access for pooled lookup
  auto GetUntyped(ResourceHandle handle) noexcept -> Component* override
  {
    return static_cast<Component*>(Get(handle));
  }

  // Type-erased access for pooled lookup
  auto GetUntyped(ResourceHandle handle) const noexcept
    -> const Component* override
  {
    return static_cast<const Component*>(Get(handle));
  }

private:
  mutable std::shared_mutex mutex_; // Thread safety for all operations
  ResourceTable<PooledComponentType> table_;
};

} // namespace oxygen
