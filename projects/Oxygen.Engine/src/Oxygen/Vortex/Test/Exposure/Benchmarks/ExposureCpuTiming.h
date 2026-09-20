//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Profiling/CpuScopeObserver.h>

namespace oxygen::vortex::testing::exposure {

//! Bounded, allocation-free collection during the steady benchmark window.
//! Raw QPC intervals are intersected with ETW scheduler data after collection.
class ExposureCpuTiming final : public profiling::CpuScopeObserver {
public:
  explicit ExposureCpuTiming(unsigned frames);
  auto BeginFrame(unsigned sequence) -> void;
  auto Save(const std::filesystem::path& path) const -> nlohmann::json;
  auto OnScopeBegin(const profiling::CpuProfileScopeDesc& desc) noexcept
    -> bool override;
  auto OnScopeEnd() noexcept -> void override;

private:
  struct Record {
    std::int64_t begin {};
    std::int64_t end {};
    unsigned frame {};
    bool wait {};
    std::array<char, 128> label {};
  };
  struct OpenScope {
    std::size_t record {};
    bool wait {};
  };
  std::vector<Record> records_;
  std::array<OpenScope, 128> stack_ {};
  std::size_t stack_depth_ {};
  std::size_t exposure_depth_ {};
  std::int64_t frequency_ {};
  unsigned process_ {};
  unsigned thread_ {};
  unsigned frame_ {};
  bool invalid_ {};
};

} // namespace oxygen::vortex::testing::exposure
