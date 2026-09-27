//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>

namespace oxygen::graphics {
class CommandRecorder;
class ResourceRegistry;
}

namespace oxygen::vortex {
class Renderer;
struct LightingFrameBindings;

namespace lighting::internal {

  struct BrdfEnergyProduct {
    graphics::ManagedTexture allocation;
    graphics::CompletionReceipt producer;

    auto Publish(LightingFrameBindings& bindings) const -> void;
    [[nodiscard]] auto Attach(graphics::ResourceRegistry& registry,
      graphics::CommandRecorder& recorder) const -> bool;
  };

  //! One immutable model product for all of a renderer's views.
  class BrdfEnergyResources final {
  public:
    explicit BrdfEnergyResources(Renderer& renderer);
    ~BrdfEnergyResources();
    OXYGEN_MAKE_NON_COPYABLE(BrdfEnergyResources)
    OXYGEN_MAKE_NON_MOVABLE(BrdfEnergyResources)

    [[nodiscard]] auto Prepare()
      -> std::expected<std::shared_ptr<const BrdfEnergyProduct>,
        LightingPreparationFailure>;

  private:
    Renderer& renderer_;
    std::shared_ptr<const BrdfEnergyProduct> product_;
  };

} // namespace lighting::internal
} // namespace oxygen::vortex
