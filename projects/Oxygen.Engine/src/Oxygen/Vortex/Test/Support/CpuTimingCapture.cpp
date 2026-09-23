//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <stdexcept>
#include <string_view>

// Initializes Windows SDK architecture and prerequisite types for API headers.
#include <Windows.h> // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>
#include <processthreadsapi.h>
#include <profileapi.h>
#include <winnt.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Test/Support/CpuTimingCapture.h>

namespace oxygen::vortex::testing {
namespace {
  constexpr auto kNoRecord = std::numeric_limits<std::size_t>::max();
  auto Timestamp() noexcept -> std::int64_t
  {
    LARGE_INTEGER value {};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
  }
} // namespace

CpuTimingCapture::CpuTimingCapture(const CpuTimingOptions options)
  : process_(GetCurrentProcessId())
  , thread_(GetCurrentThreadId())
  , options_(options)
{
  if (options.record_capacity.get() == 0U
    || (options.domain != CpuTimingDomain::kExposure
      && options.domain != CpuTimingDomain::kLighting)) {
    throw std::invalid_argument(
      "CPU timing requires a valid domain and nonzero capacity");
  }
  LARGE_INTEGER frequency {};
  QueryPerformanceFrequency(&frequency);
  frequency_ = frequency.QuadPart;
  records_.reserve(options.record_capacity.get());
}

auto CpuTimingCapture::BeginFrame(const frame::SequenceNumber sequence) -> void
{
  if (stack_depth_ != 0U || root_depth_ != 0U || thread_ != GetCurrentThreadId()
    || sequence == frame::kInvalidSequenceNumber
    || (has_frame_ && sequence <= frame_)) {
    invalid_ = true;
    throw std::logic_error(
      "Incomplete, nonmonotonic or cross-thread CPU timing frame");
  }
  frame_ = sequence;
  has_frame_ = true;
}

auto CpuTimingCapture::OnScopeBegin(
  const profiling::CpuProfileScopeDesc& desc) noexcept -> bool
try {
  if (!has_frame_ || thread_ != GetCurrentThreadId()) {
    invalid_ = true;
    return false;
  }
  const auto label = std::string_view { desc.label };
  // Charge the entire shared view owner to exposure. Moving acquisition and
  // submission outside the pass scopes must not remove them from attribution.
  const bool view_recording = (label == "Graphics.AcquireCommandRecorder"
                                || label == "Graphics.FinalizeCommandRecorder")
    && std::ranges::any_of(desc.variables, [](const auto& variable) -> bool {
         return variable.key != nullptr
           && std::string_view(variable.key.get()) == "recording"
           && variable.value == "Vortex View";
       });
  const bool exposure = (label.starts_with("Vortex.PostProcess.")
                          && label != "Vortex.PostProcess.Execute")
    || label == "Vortex.SceneRenderer.PrepareExposureDomain" || view_recording;
  const bool root = options_.domain == CpuTimingDomain::kExposure
    ? exposure
    : label.starts_with("Vortex.Lighting.")
      || label.starts_with("Vortex.Shadows.");
  const bool wait = root_depth_ != 0U && label == "D3D12.FenceWait";
  const bool detail = options_.detailed && root_depth_ != 0U
    && (label.starts_with("Graphics.") || label.starts_with("D3D12."));
  if (!root && !wait && !detail) {
    return false;
  }
  if (stack_depth_ == stack_.size()) {
    invalid_ = true;
    return false;
  }
  auto record = kNoRecord;
  if (options_.detailed || wait || root_depth_ == 0U) {
    if (records_.size() == options_.record_capacity.get()
      || label.size() >= Record {}.label.size()
      || label.find_first_of(",\r\n\"") != std::string_view::npos) {
      invalid_ = true;
    } else {
      record = records_.size();
      auto& value = records_.emplace_back();
      value.frame = frame_;
      const auto scope_kind = root_depth_ == 0U ? Kind::kRoot : Kind::kDetail;
      value.kind = wait ? Kind::kFenceWait : scope_kind;
      std::memcpy(value.label.data(), label.data(), label.size());
      value.begin = Timestamp();
    }
  }
  stack_.at(stack_depth_++) = { .record = record, .root = root };
  if (root) {
    ++root_depth_;
  }
  return true;
} catch (...) {
  // Observer callbacks must not let diagnostic failures escape into rendering.
  invalid_ = true;
  return false;
}

auto CpuTimingCapture::OnScopeEnd() noexcept -> void
try {
  if (stack_depth_ == 0U || thread_ != GetCurrentThreadId()) {
    invalid_ = true;
    return;
  }
  const auto scope = stack_.at(--stack_depth_);
  if (scope.record != kNoRecord) {
    records_.at(scope.record).end = Timestamp();
  }
  if (scope.root) {
    --root_depth_;
  }
} catch (...) {
  invalid_ = true;
}

auto CpuTimingCapture::Save(const std::filesystem::path& path) const
  -> nlohmann::json
{
  if (invalid_ || stack_depth_ != 0U || root_depth_ != 0U || records_.empty()
    || std::filesystem::exists(path)) {
    throw std::runtime_error("Invalid CPU timing capture or existing evidence");
  }
  std::ofstream output(path, std::ios::binary);
  output << "frame_seq,thread_id,kind,start_qpc,end_qpc,label\n";
  for (const auto& record : records_) {
    if (record.end < record.begin) {
      throw std::runtime_error("Incomplete CPU timing interval");
    }
    const auto* const non_root_kind
      = record.kind == Kind::kFenceWait ? "fence_wait" : "detail";
    const auto* const root_kind
      = options_.domain == CpuTimingDomain::kExposure ? "exposure" : "lighting";
    const auto* const kind
      = record.kind == Kind::kRoot ? root_kind : non_root_kind;
    output << record.frame.get() << ',' << thread_ << ',' << kind << ','
           << record.begin << ',' << record.end << ',' << record.label.data()
           << '\n';
  }
  output.close();
  if (!output.good()) {
    throw std::runtime_error("Could not write CPU timing evidence");
  }
  return {
    { "path", path.filename().string() },
    { "qpc_frequency", frequency_ },
    { "process_id", process_ },
    { "thread_id", thread_ },
    { "records", records_.size() },
    { "complete", true },
    { "detailed", options_.detailed },
    { "record_capacity", options_.record_capacity.get() },
  };
}

} // namespace oxygen::vortex::testing
