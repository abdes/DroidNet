//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>

namespace oxygen::vortex::testing {

using MemorySampleCapacity
  = NamedType<std::size_t, struct MemorySampleCapacityTag>;
using MemorySampleId
  = NamedType<std::uint64_t, struct MemorySampleIdTag, Comparable>;

//! Test/benchmark-only bounded capture; formatting happens after collection.
//! The caller serializes Record calls and supplies ordered sample identities.
class D3D12MemoryCapture final {
public:
  explicit D3D12MemoryCapture(MemorySampleCapacity capacity);
  [[nodiscard]] auto Record(MemorySampleId sample, frame::SequenceNumber frame,
    const graphics::d3d12::MemoryStatistics& statistics) noexcept -> bool;
  //! Refuses empty, overflowing, unordered or inconsistent captures.
  [[nodiscard]] auto Report() const -> nlohmann::json;

private:
  struct Sample {
    MemorySampleId id { 0U };
    frame::SequenceNumber frame { 0U };
    graphics::d3d12::MemoryStatistics statistics;
  };
  std::vector<Sample> samples_;
  MemorySampleCapacity capacity_;
  bool invalid_ { false };
};

} // namespace oxygen::vortex::testing
