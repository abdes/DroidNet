//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace oxygen::vortex {

namespace lighting::internal {

  struct BuiltLightGridView {
    ViewId view_id { kInvalidViewId };
    LightingFrameBindings bindings {};
    LightGridMetadata metadata {};
  };

  struct BuiltLightGridFrame {
    LightEvaluationRecords evaluation;
    std::vector<BuiltLightGridView> per_view;
    std::uint64_t selection_epoch { 0U };
  };

  class LightGridBuilder {
  public:
    struct BuildStats {
      frame::SequenceNumber frame_sequence { 0U };
      frame::Slot frame_slot { frame::kInvalidSlot };
      std::uint32_t build_count { 0U };
      std::uint32_t published_view_count { 0U };
      std::uint32_t directional_light_count { 0U };
      std::uint32_t local_light_count { 0U };
      std::uint64_t selection_epoch { 0U };
    };

    auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void;
    [[nodiscard]] auto Build(const FrameLightingInputs& inputs)
      -> std::expected<BuiltLightGridFrame, LightingPreparationFailure>;
    [[nodiscard]] auto GetLastBuildStats() const noexcept -> const BuildStats&
    {
      return last_build_stats_;
    }

  private:
    BuildStats last_build_stats_ {};
  };

} // namespace lighting::internal

} // namespace oxygen::vortex
