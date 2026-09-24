//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {
struct FrameLightSelection;
struct ShadowFrameData;

namespace shadows::internal {

  //! Build dense selection maps from actual projection records, never kind
  //! counters.
  [[nodiscard]] OXGN_VRTX_API auto BuildShadowReferences(
    const FrameLightSelection& selection, ShadowFrameData& data,
    const ResolvedView* view = nullptr)
    -> std::expected<void, LightingPreparationFailure>;

} // namespace shadows::internal
} // namespace oxygen::vortex
