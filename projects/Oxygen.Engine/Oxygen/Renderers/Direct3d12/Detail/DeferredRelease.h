//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12::detail {

  class DeferredReleaseTracker
  {
  public:
    static DeferredReleaseTracker& Instance()
    {
      static DeferredReleaseTracker instance;
      return instance;
    }

    void DeferRelease(IUnknown* resource) noexcept;
    void ProcessDeferredReleases(size_t frame_index);

    void Initialize(DeferredReleaseControllerPtr renderer)
    {
      renderer_ = std::move(renderer);
    }

  private:
    DeferredReleaseTracker() = default;
    ~DeferredReleaseTracker() = default;

    OXYGEN_MAKE_NON_COPYABLE(DeferredReleaseTracker);
    OXYGEN_MAKE_NON_MOVEABLE(DeferredReleaseTracker);

    std::vector<IUnknown*> deferred_releases_[kFrameBufferCount]{};
    DeferredReleaseControllerPtr renderer_;
    std::mutex mutex_;
  };

  template<typename T>
  void DeferredObjectRelease(T*& resource) noexcept
  {
    if (resource) {
      auto& instance = DeferredReleaseTracker::Instance();
      instance.DeferRelease(resource);
      resource = nullptr;
    }
  }

}  // namespace oxygen::renderer::direct3d12::detail
