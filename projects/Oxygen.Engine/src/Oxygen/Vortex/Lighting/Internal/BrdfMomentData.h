//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::lighting::internal {

inline constexpr std::uint32_t kBrdfModelRevision = 1U;

enum class BrdfMomentDataError : std::uint8_t {
  kModelMismatch,
  kCorruptPayload,
};

struct BrdfMomentData {
  std::uint32_t model_revision;
  std::uint32_t view_nodes;
  std::uint32_t roughness_nodes;
  std::span<const std::byte> moments;
  std::span<const std::byte> means;
  base::Sha256Digest payload_hash;
};

//! Immutable compiled model data, verified once before GPU publication.
[[nodiscard]] OXGN_VRTX_API auto GetBrdfMomentData()
  -> std::expected<BrdfMomentData, BrdfMomentDataError>;

} // namespace oxygen::vortex::lighting::internal
