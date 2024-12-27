#pragma once

#include "Oxygen/Renderers/Common/MixinDeferredRelease.h"

namespace oxygen::renderer::d3d12::detail {

  [[nodiscard]] PerFrameResourceManager& GetPerFrameResourceManager();

  template<typename T>
  void DeferredObjectRelease(T*& resource) noexcept
  {
    if (resource) {
      GetPerFrameResourceManager().RegisterDeferredRelease(resource);
    }
  }

}  // namespace oxygen::renderer::d3d12::detail
