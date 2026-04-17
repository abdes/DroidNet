//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Lighting/Internal/ForwardLightPublisher.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::lighting::internal {

ForwardLightPublisher::ForwardLightPublisher(Renderer& renderer)
  : renderer_(renderer)
{
}

auto ForwardLightPublisher::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_bindings_.clear();
}

auto ForwardLightPublisher::Publish(const BuiltLightGridFrame& built_frame) -> void
{
  static_cast<void>(renderer_);
  static_cast<void>(current_sequence_);
  static_cast<void>(current_slot_);

  published_bindings_.clear();
  for (const auto& view : built_frame.per_view) {
    published_bindings_.insert_or_assign(view.view_id, view.bindings);
  }
}

auto ForwardLightPublisher::InspectBindings(const ViewId view_id) const
  -> const LightingFrameBindings*
{
  const auto it = published_bindings_.find(view_id);
  return it != published_bindings_.end() ? &it->second : nullptr;
}

} // namespace oxygen::vortex::lighting::internal
