//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstddef>
#include <expected>
#include <span>

#include "GeneratedGgxEnergyData.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyData.h>

namespace oxygen::vortex::lighting::internal {

auto GetBrdfEnergyData() -> std::expected<BrdfEnergyData, BrdfEnergyDataError>
{
  static_assert(std::endian::native == std::endian::little);
  static const auto result
    = [] -> std::expected<BrdfEnergyData, BrdfEnergyDataError> {
    constexpr auto kBytesPerTexel = sizeof(float) * 2U;
    const auto bytes = std::as_bytes(std::span(generated::kEnergyWords));
    const auto energy_size = static_cast<std::size_t>(generated::kViewNodes)
      * generated::kRoughnessNodes * kBytesPerTexel;
    if (generated::kModelRevision != kBrdfModelRevision) {
      return std::unexpected(BrdfEnergyDataError::kModelMismatch);
    }
    if (bytes.size() != energy_size
      || base::ComputeSha256(bytes) != generated::kPayloadHash) {
      return std::unexpected(BrdfEnergyDataError::kCorruptPayload);
    }
    return BrdfEnergyData {
      .model_revision = generated::kModelRevision,
      .view_nodes = generated::kViewNodes,
      .roughness_nodes = generated::kRoughnessNodes,
      .energy = bytes,
      .payload_hash = generated::kPayloadHash,
    };
  }();
  return result;
}

} // namespace oxygen::vortex::lighting::internal
