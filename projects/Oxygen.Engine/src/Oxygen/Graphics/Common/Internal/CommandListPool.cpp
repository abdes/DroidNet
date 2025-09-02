//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>

namespace oxygen::graphics::internal {

/*!
 Initializes the pool with a factory function that will be used to create new
 command list instances when the pool is empty or when additional capacity is
 needed.

 @param factory Function that creates command lists for specific queue roles and
 with given names. Must not be null.

 @note The factory function should be thread-safe as it may be called
 concurrently from multiple threads.
 @see CommandListFactory
*/
CommandListPool::CommandListPool(CommandListFactory factory)
  : factory_(std::move(factory))
{
  if (!factory_) {
    throw std::invalid_argument("CommandListPool requires a valid factory");
  }
}

CommandListPool::~CommandListPool() { Clear(); }

/*!
 Removes and destroys all command lists currently stored in the pool across all
 queue roles. This operation is thread-safe and will block until all concurrent
 operations complete.

 @note This method should typically be called during shutdown or when a complete
 reset of the pool is required. Command lists currently in use (held by
 shared_ptr) are not affected.
*/
auto CommandListPool::Clear() noexcept -> void
{
  std::lock_guard lock(command_list_pool_mutex_);
  command_list_pool_.clear();
}

/*!
 Retrieves a command list from the pool, reusing an existing one if available or
 creating a new one using the factory function. The returned command list is
 wrapped in a shared_ptr with a custom deleter that automatically returns it to
 the pool when the reference count reaches zero.

 @param queue_role The graphics queue role for which the command list is needed
 @param command_list_name A descriptive name for debugging and profiling
 purposes

 @return A shared_ptr to a command list ready for use. The command list will be
 automatically returned to the pool when all references are released.

 ### Performance Characteristics

 - Time Complexity: O(1) amortized for pool hits, O(factory) for creation
 - Memory: Reuses existing allocations when possible
 - Optimization: Separate pools per queue role minimize contention

 ### Usage Examples

 ```cpp
 {
   auto cmd_list = pool.AcquireCommandList(
     QueueRole::Graphics, "MainRenderPass");
   // Record commands...
 }
 // Command list automatically returned to pool when cmd_list goes out of scope
 ```

 @note This method is thread-safe and can be called concurrently from multiple
 threads.
*/
auto CommandListPool::AcquireCommandList(graphics::QueueRole queue_role,
  std::string_view command_list_name) -> std::shared_ptr<graphics::CommandList>
{
  // Acquire or create a command list
  std::unique_ptr<graphics::CommandList> cmd_list;
  {
    std::lock_guard lock(command_list_pool_mutex_);

    if (auto& pool = command_list_pool_[queue_role]; pool.empty()) {
      // Create a new command list if pool is empty
      cmd_list = factory_(queue_role, command_list_name);
    } else {
      // Take one from the pool
      cmd_list = std::move(pool.back());
      pool.pop_back();
      cmd_list->SetName(command_list_name);
    }
  }

  // Create a shared_ptr with custom deleter that returns the command list to
  // the pool
  return { cmd_list.get(),
    [this, queue_role, cmd_list_raw = cmd_list.release()](
      graphics::CommandList*) mutable {
      cmd_list_raw->SetName("Recycled Command List");
      // Create a new unique_ptr that owns the command list
      auto recycled_cmd_list
        = std::unique_ptr<graphics::CommandList>(cmd_list_raw);

      // Return to pool
      std::lock_guard<std::mutex> lock(command_list_pool_mutex_);
      command_list_pool_[queue_role].push_back(std::move(recycled_cmd_list));
    } };

  // The Original shared_ptr will be destroyed, but the command list is now
  // managed by the custom deleter and will be returned to the pool when the
  // returned shared_ptr is destroyed
}

} // namespace oxygen::graphics::internal
