//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>

namespace oxygen::vortex::testing {

enum class CpuTimingDomain : std::uint8_t { kExposure, kLighting };
using CpuTimingRecordCapacity
  = NamedType<std::size_t, struct CpuTimingRecordCapacityTag>;

struct CpuTimingOptions {
  CpuTimingDomain domain { CpuTimingDomain::kLighting };
  CpuTimingRecordCapacity record_capacity { 0U };
  bool detailed { false };
};

//! Bounded, allocation-free collection during the steady benchmark window.
//! Raw QPC intervals are intersected with ETW scheduler data after collection.
class CpuTimingCapture final : public profiling::CpuScopeObserver {
public:
  explicit CpuTimingCapture(CpuTimingOptions options);
  auto BeginFrame(frame::SequenceNumber sequence) -> void;
  [[nodiscard]] auto Save(const std::filesystem::path& path) const
    -> nlohmann::json;
  auto OnScopeBegin(const profiling::CpuProfileScopeDesc& desc) noexcept
    -> bool override;
  auto OnScopeEnd() noexcept -> void override;

private:
  static constexpr std::size_t kLabelCapacity = 128U;
  static constexpr std::size_t kMaximumScopeDepth = 128U;
  enum class Kind : std::uint8_t { kRoot, kFenceWait, kDetail };
  struct Record {
    std::int64_t begin {};
    std::int64_t end {};
    frame::SequenceNumber frame { 0U };
    Kind kind {};
    std::array<char, kLabelCapacity> label {};
  };
  struct OpenScope {
    std::size_t record {};
    bool root {};
  };
  std::vector<Record> records_;
  std::array<OpenScope, kMaximumScopeDepth> stack_ {};
  std::size_t stack_depth_ {};
  std::size_t root_depth_ {};
  std::int64_t frequency_ {};
  unsigned process_ {};
  unsigned thread_ {};
  frame::SequenceNumber frame_ { 0U };
  CpuTimingOptions options_;
  bool has_frame_ { false };
  bool invalid_ {};
};

} // namespace oxygen::vortex::testing
