//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>

namespace oxygen::vortex {

class Renderer;

namespace lighting::internal {

class ForwardLightPublisher {
public:
  explicit ForwardLightPublisher(Renderer& renderer);

  auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void;
  auto Publish(const BuiltLightGridFrame& built_frame) -> void;
  [[nodiscard]] auto InspectBindings(ViewId view_id) const
    -> const LightingFrameBindings*;

private:
  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::unordered_map<ViewId, LightingFrameBindings> published_bindings_ {};
};

} // namespace lighting::internal

} // namespace oxygen::vortex
