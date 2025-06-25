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
   Allocates a new component in the pool, forwarding arguments to the
   component's constructor.

   @tparam Args Argument types for the component's constructor

   @param args Arguments to forward to the component's constructor
   @return Handle to the newly allocated component

   @note Thread-safe (exclusive lock). May invalidate pointers if pool grows.

   @see Deallocate, Get
  */
  template <typename... Args> auto Allocate(Args&&... args) -> Handle
  {
    std::lock_guard lock(mutex_);
    return table_.Emplace(std::forward<Args>(args)...);
  }

  /*!
   Allocates a new component in the pool by moving the given component.

   @param comp Component to move into the pool. Must match the pool's type.
   @return Handle to the new component
   @throws std::invalid_argument if the type does not match

   @note Thread-safe (exclusive lock). May invalidate pointers if pool grows.

   @see Allocate, Deallocate
  */
  auto Allocate(Component&& comp) -> ResourceHandle override
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
   Removes a component from the pool and invalidates its handle.

   @param handle Handle to the component to remove
   @return 1 if the component was removed, 0 if not found

   @note Thread-safe (exclusive lock). May invalidate pointers if pool grows.

   @see Allocate
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
   Returns a pointer to the component for the given handle, or nullptr if the
   handle is invalid.

   @param handle Handle to the component
   @return Pointer to the component, or nullptr if not found or invalid

   @note Thread-safe (shared lock)
   @warning Returned pointer is only valid as long as the pool is not modified

   @see Allocate, Deallocate
  */
  auto Get(Handle handle) noexcept -> PooledComponentType*
  {
    return const_cast<PooledComponentType*>(std::as_const(*this).Get(handle));
  }

  /*!
   Returns a const pointer to the component for the given handle, or nullptr if
   the handle is invalid.

   @param handle Handle to the component
   @return Const pointer to the component, or nullptr if not found or invalid

   @note Thread-safe (shared lock)
   @warning Returned pointer is only valid as long as the pool is not modified

   @see Allocate, Deallocate
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
   Defragments the pool using a custom comparison function.

   @tparam Compare Comparison function type

   @param comp Comparison function for ordering components
   @param max_swaps Maximum number of swaps to perform (0 = unlimited)
   @return Number of swaps performed

   @note Thread-safe (exclusive lock). Will invalidate pointers.

   @see Defragment(std::size_t)
  */
  template <typename Compare>
  auto Defragment(Compare comp, std::size_t max_swaps = 0) -> std::size_t
  {
    std::lock_guard lock(mutex_);
    return table_.Defragment(comp, max_swaps);
  }

  /*!
   Defragments the pool using the component's static Compare method if
   available.

   @param max_swaps Maximum number of swaps to perform (0 = unlimited)
   @return Number of swaps performed

   @note Thread-safe (exclusive lock). Will invalidate pointers.

   @see Defragment(Compare, std::size_t)
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

  /*!
   Applies a callable to every component in the pool in dense storage order,
   holding a shared lock for the duration of the iteration.

   @tparam Func Callable type; must accept a const reference to the component

   @param func Callable to invoke for each component

   @note Thread-safe (shared lock). The pool is read-locked for the duration of
   the call, so no modifications can occur concurrently.

   @see Size, IsEmpty
  */
  template <typename Func> void ForEach(Func&& func) const
  {
    std::shared_lock lock(mutex_);
    std::ranges::for_each(table_.Items(), std::forward<Func>(func));
  }

  /*!
   Applies a callable to every component in the pool in dense storage order,
   holding a shared lock for the duration of the iteration. This allows mutation
   of the component being visited, but will not work if the table is mutated (by
   adding or removing items for example).

   @tparam Func Callable type; must accept a const reference to the component

   @param func Callable to invoke for each component

   @note Thread-safe (shared lock). The pool is read-locked for the duration of
   the call, so no modifications can occur concurrently.

   @see Size, IsEmpty
  */
  template <typename Func> void ForEachMut(Func&& func)
  {
    std::lock_guard lock(mutex_);
    std::ranges::for_each(table_.Items(), std::forward<Func>(func));
  }

  //! Returns the number of components currently in the pool.
  /*!
   @return Number of components
   @note Thread-safe (shared lock)
  */
  auto Size() const noexcept -> std::size_t
  {
    std::shared_lock lock(mutex_);
    return table_.Size();
  }

  //! Check if the pool is empty.
  /*!
   @return True if empty, false otherwise
   @note Thread-safe (shared lock)
  */
  auto IsEmpty() const noexcept -> bool
  {
    std::shared_lock lock(mutex_);
    return table_.IsEmpty();
  }

  //! Get the resource type for this component pool.
  /*!
   @return Resource type ID
   @note No locking required; resource type is immutable
  */
  auto GetComponentType() const noexcept -> ResourceHandle::ResourceTypeT
  {
    return table_.GetItemType();
  }

  //! Clear all components from the pool, invalidating all handles.
  /*!
   Removes all components from the pool, invalidating all handles.

   @note Thread-safe (exclusive lock)
   @warning Only use in error recovery or test scenarios; normal lifecycle is
   managed by compositions
  */
  auto ForceClear() noexcept -> void
  {
    std::lock_guard lock(mutex_);
    table_.Clear();
  }

  //! Type-erased access for pooled lookup (non-const).
  /*!
   Returns a pointer to the component for the given handle, as a base
   `Component*`. Used by type-erased pool interfaces and the registry.

   @param handle Handle to the component
   @return Pointer to component or nullptr if handle is invalid

   @note Used internally for type-erased access; not intended for direct use.

   @see ComponentPoolUntyped
  */
  auto GetUntyped(ResourceHandle handle) noexcept -> Component* override
  {
    return static_cast<Component*>(Get(handle));
  }

  //! Type-erased access for pooled lookup (const).
  /*!
   Returns a const pointer to the component for the given handle, as a base
   `const Component*`. Used by type-erased pool interfaces and the registry.

   @param handle Handle to the component
   @return Const pointer to component or nullptr if handle is invalid

   @note Used internally for type-erased access; not intended for direct use.

   @see ComponentPoolUntyped
  */
  auto GetUntyped(ResourceHandle handle) const noexcept
    -> const Component* override
  {
    return static_cast<const Component*>(Get(handle));
  }

private:
  ResourceTable<PooledComponentType> table_;

  mutable std::shared_mutex mutex_; // Thread safety for all operations
};

} // namespace oxygen
