//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::internal {

//! Thread-safe pool for managing command lists across different queue roles.
/*!
 A high-performance pool that efficiently manages command list lifecycle by
 reusing existing objects and creating new ones only when necessary. The pool
 maintains separate collections for each queue role to optimize resource access
 patterns.

 ### Key Features

 - **Thread Safe**
 - **Queue Role Separation**: dedicated pools per graphics queue type
 - **Dependency Injection**: Uses configurable factory function for actual
   command list creation

 ### Usage Patterns

 The pool is designed for high-frequency acquire/release cycles typical in
 graphics rendering pipelines. Command lists are acquired for recording commands
 and automatically returned to the pool when the shared_ptr reference count
 reaches zero.

 ### Architecture Notes

 This class is part of the internal graphics API and should not be used directly
 by client code. It integrates with the Component system for lifecycle
 management and provides the foundation for efficient command buffer management
 across different graphics backends.

 @see graphics::AcquireCommandList
*/
class CommandListPool final : public Component {
  OXYGEN_COMPONENT(CommandListPool)

public:
  //! Factory function that takes a queue role and a command list name and
  //! returns a std::unique_ptr to a new command list.
  using CommandListFactory
    = std::function<std::unique_ptr<graphics::CommandList>(
      graphics::QueueRole, std::string_view)>;

  //! Constructs a command list pool with the specified factory function.
  OXGN_GFX_API explicit CommandListPool(CommandListFactory factory);

  OXYGEN_MAKE_NON_COPYABLE(CommandListPool)
  OXYGEN_DEFAULT_MOVABLE(CommandListPool)

  OXGN_GFX_API ~CommandListPool() override;

  //! Clears all cached command lists from the pool.
  OXGN_GFX_API auto Clear() noexcept -> void;

  //! Acquires a command list from the pool for the specified queue role.
  OXGN_GFX_NDAPI auto AcquireCommandList(
    graphics::QueueRole queue_role, std::string_view command_list_name)
    -> std::shared_ptr<graphics::CommandList>;

private:
  CommandListFactory factory_;

  using CommandListUniquePtr = std::unique_ptr<graphics::CommandList>;
  using CommandLists = std::vector<CommandListUniquePtr>;
  //! Pool of available command lists by queue type.
  std::unordered_map<graphics::QueueRole, CommandLists> command_list_pool_;
  std::mutex command_list_pool_mutex_;
};

} // namespace oxygen::graphics::internal
