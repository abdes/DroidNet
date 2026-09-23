//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {
class Renderer;
struct LightingFrameBindings;

namespace lighting::internal {

  //! One immutable model product for all of a renderer's views.
  class BrdfEnergyResources final {
  public:
    explicit BrdfEnergyResources(Renderer& renderer);
    ~BrdfEnergyResources();
    OXYGEN_MAKE_NON_COPYABLE(BrdfEnergyResources)
    OXYGEN_MAKE_NON_MOVABLE(BrdfEnergyResources)

    auto Prepare() -> std::expected<void, LightingPreparationFailure>;
    auto Publish(LightingFrameBindings& bindings) const -> void;

  private:
    Renderer& renderer_;
    std::shared_ptr<graphics::Texture> texture_;
    ShaderVisibleIndex slot_ { kInvalidShaderVisibleIndex };
    bool initialized_ { false };
  };

} // namespace lighting::internal
} // namespace oxygen::vortex
