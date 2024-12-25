//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/ResourceHandle.h"

namespace oxygen {

  /**
   * A graphics API agnostic POD structure representing different types of
   * resources that get linked to their counterparts on the core backend.
   */
  template <ResourceHandle::ResourceTypeT ResourceType>
  class Resource
  {
  public:
    constexpr explicit Resource(ResourceHandle handle)
      : handle_(std::move(handle)) {
      assert(handle_.ResourceType() == ResourceType);
    }

    virtual ~Resource() = default;

    OXYGEN_DEFAULT_COPYABLE(Resource);
    OXYGEN_DEFAULT_MOVABLE(Resource);

    [[nodiscard]] constexpr auto GetId() const noexcept -> const ResourceHandle& {
      return handle_;
    }

    [[nodiscard]] virtual auto IsValid() const noexcept -> bool {
      return handle_.IsValid();
    }

  protected:
    constexpr Resource() = default;  // Create an invalid resource
    constexpr void Invalidate() { handle_.Invalidate(); }

  private:
    ResourceHandle handle_;
  };

}  // namespace oxygen
