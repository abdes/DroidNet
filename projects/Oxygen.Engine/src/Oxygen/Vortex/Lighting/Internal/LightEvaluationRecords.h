//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <vector>

#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::lighting::internal {

struct LightEvaluationRecords {
  std::vector<DirectionalLightForwardData> directional;
  std::vector<ForwardLocalLightRecord> local;
};

[[nodiscard]] auto ResolveLightEvaluationRecords(
  const FrameLightSelection& input)
  -> std::expected<LightEvaluationRecords, LightingPreparationFailure>;

} // namespace oxygen::vortex::lighting::internal
