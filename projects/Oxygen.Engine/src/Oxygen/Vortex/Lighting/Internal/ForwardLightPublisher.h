//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

class Renderer;

namespace lighting::internal {

  struct PublishedLightingView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    LightingFrameBindings bindings {};
  };

  class ForwardLightPublisher {
  public:
    explicit ForwardLightPublisher(Renderer& renderer);
    ~ForwardLightPublisher() = default;

    OXYGEN_MAKE_NON_COPYABLE(ForwardLightPublisher)
    OXYGEN_MAKE_NON_MOVABLE(ForwardLightPublisher)

    auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void;
    auto InvalidateViews() -> void { published_views_.clear(); }
    [[nodiscard]] auto Publish(const BuiltLightGridFrame& built_frame)
      -> std::expected<void, LightingPreparationFailure>;
    [[nodiscard]] auto InspectBindings(ViewId view_id) const
      -> const LightingFrameBindings*;
    [[nodiscard]] auto ResolveBindingSlot(ViewId view_id) const
      -> ShaderVisibleIndex;

  private:
    auto EnsurePublishResources() -> bool;

    Renderer& renderer_;
    frame::SequenceNumber current_sequence_ { 0U };
    frame::Slot current_slot_ { frame::kInvalidSlot };
    std::unique_ptr<::oxygen::vortex::internal::PerViewStructuredPublisher<
      LightingFrameBindings>>
      lighting_bindings_publisher_;
    std::unique_ptr<upload::TransientStructuredBuffer> local_light_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer> grid_metadata_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer> grid_indirection_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer>
      directional_light_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer> build_status_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer> local_shadow_map_buffer_;
    std::unique_ptr<upload::TransientStructuredBuffer>
      directional_shadow_map_buffer_;
    std::unordered_map<ViewId, PublishedLightingView> published_views_;
  };

} // namespace lighting::internal

} // namespace oxygen::vortex
