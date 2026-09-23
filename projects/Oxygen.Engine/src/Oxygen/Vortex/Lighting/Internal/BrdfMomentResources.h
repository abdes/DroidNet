//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
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
  class BrdfMomentResources final {
  public:
    explicit BrdfMomentResources(Renderer& renderer);
    ~BrdfMomentResources();
    OXYGEN_MAKE_NON_COPYABLE(BrdfMomentResources)
    OXYGEN_MAKE_NON_MOVABLE(BrdfMomentResources)

    auto Prepare() -> std::expected<void, LightingPreparationFailure>;
    auto Publish(LightingFrameBindings& bindings) const -> void;

  private:
    Renderer& renderer_;
    std::array<std::shared_ptr<graphics::Texture>, 2> textures_;
    std::array<ShaderVisibleIndex, 2> slots_ {
      kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex,
    };
    bool initialized_ { false };
  };

} // namespace lighting::internal
} // namespace oxygen::vortex
