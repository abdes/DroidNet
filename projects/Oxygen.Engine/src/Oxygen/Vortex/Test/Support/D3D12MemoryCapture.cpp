//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>
#include <Oxygen/Vortex/Test/Support/D3D12MemoryCapture.h>

namespace oxygen::vortex::testing {
namespace {
  auto SegmentReport(const graphics::d3d12::MemorySegmentStatistics& segment)
    -> nlohmann::json
  {
    return {
      { "allocation_count", segment.allocation_count },
      { "block_count", segment.block_count },
      { "allocation_bytes", segment.allocation_bytes.get() },
      { "block_bytes", segment.block_bytes.get() },
      {
        "unallocated_block_bytes",
        segment.block_bytes.get() - segment.allocation_bytes.get(),
      },
      { "estimated_usage_bytes", segment.estimated_usage_bytes.get() },
      { "estimated_budget_bytes", segment.estimated_budget_bytes.get() },
    };
  }
} // namespace

D3D12MemoryCapture::D3D12MemoryCapture(const MemorySampleCapacity capacity)
  : capacity_(capacity)
{
  if (capacity.get() == 0U) {
    throw std::invalid_argument("Memory capture requires nonzero capacity");
  }
  samples_.reserve(capacity.get());
}

auto D3D12MemoryCapture::Record(const MemorySampleId sample,
  const frame::SequenceNumber frame,
  const graphics::d3d12::MemoryStatistics& statistics) noexcept -> bool
try {
  if (invalid_ || samples_.size() == capacity_.get()
    || frame == frame::kInvalidSequenceNumber
    || (!samples_.empty()
      && (sample <= samples_.back().id || frame < samples_.back().frame))
    || statistics.local.allocation_bytes.get()
      > statistics.local.block_bytes.get()
    || statistics.non_local.allocation_bytes.get()
      > statistics.non_local.block_bytes.get()) {
    invalid_ = true;
    return false;
  }
  // Reserved storage plus a scalar-only record makes collection
  // allocation-free.
  samples_.push_back(
    { .id = sample, .frame = frame, .statistics = statistics });
  return true;
} catch (...) {
  // A diagnostic failure must not escape into the renderer or leave a
  // reportable partial capture.
  invalid_ = true;
  return false;
}

auto D3D12MemoryCapture::Report() const -> nlohmann::json
{
  if (invalid_ || samples_.empty()) {
    throw std::runtime_error("Invalid or empty memory capture");
  }
  auto rows = nlohmann::json::array();
  for (const auto& sample : samples_) {
    rows.push_back({
      { "sample_id", sample.id.get() },
      { "frame_sequence", sample.frame.get() },
      { "local", SegmentReport(sample.statistics.local) },
      { "non_local", SegmentReport(sample.statistics.non_local) },
    });
  }
  return {
    { "schema", 1U },
    { "complete", true },
    { "capacity", capacity_.get() },
    { "samples", std::move(rows) },
  };
}

} // namespace oxygen::vortex::testing
