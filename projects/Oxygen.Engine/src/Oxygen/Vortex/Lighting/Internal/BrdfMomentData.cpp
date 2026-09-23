//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstddef>
#include <expected>
#include <span>

#include "GeneratedGgxMomentData.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfMomentData.h>

namespace oxygen::vortex::lighting::internal {

auto GetBrdfMomentData() -> std::expected<BrdfMomentData, BrdfMomentDataError>
{
  static_assert(std::endian::native == std::endian::little);
  static const auto result
    = [] -> std::expected<BrdfMomentData, BrdfMomentDataError> {
    constexpr auto kBytesPerMoment = sizeof(float) * 2U;
    const auto bytes = std::as_bytes(std::span(generated::kMomentWords));
    const auto moments_size = static_cast<std::size_t>(generated::kViewNodes)
      * generated::kRoughnessNodes * kBytesPerMoment;
    const auto means_size
      = static_cast<std::size_t>(generated::kRoughnessNodes) * kBytesPerMoment;
    if (generated::kModelRevision != kBrdfModelRevision) {
      return std::unexpected(BrdfMomentDataError::kModelMismatch);
    }
    if (bytes.size() != moments_size + means_size
      || base::ComputeSha256(bytes) != generated::kPayloadHash) {
      return std::unexpected(BrdfMomentDataError::kCorruptPayload);
    }
    return BrdfMomentData {
      .model_revision = generated::kModelRevision,
      .view_nodes = generated::kViewNodes,
      .roughness_nodes = generated::kRoughnessNodes,
      .moments = bytes.first(moments_size),
      .means = bytes.subspan(moments_size, means_size),
      .payload_hash = generated::kPayloadHash,
    };
  }();
  return result;
}

} // namespace oxygen::vortex::lighting::internal
