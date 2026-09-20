//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureCpuTiming.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr auto kNoRecord = std::numeric_limits<std::size_t>::max();
  auto Timestamp() noexcept -> std::int64_t
  {
    LARGE_INTEGER value {};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
  }
} // namespace

ExposureCpuTiming::ExposureCpuTiming(const unsigned frames)
  : process_(GetCurrentProcessId())
  , thread_(GetCurrentThreadId())
{
  LARGE_INTEGER frequency {};
  QueryPerformanceFrequency(&frequency);
  frequency_ = frequency.QuadPart;
  records_.reserve(static_cast<std::size_t>(frames) * 64U);
}

auto ExposureCpuTiming::BeginFrame(const unsigned sequence) -> void
{
  if (stack_depth_ != 0U || exposure_depth_ != 0U
    || thread_ != GetCurrentThreadId()) {
    throw std::logic_error("Incomplete or cross-thread CPU timing frame");
  }
  frame_ = sequence;
}

auto ExposureCpuTiming::OnScopeBegin(
  const profiling::CpuProfileScopeDesc& desc) noexcept -> bool
{
  const auto label = std::string_view { desc.label };
  const bool exposure = (label.starts_with("Vortex.PostProcess.")
                          && label != "Vortex.PostProcess.Execute")
    || label == "Vortex.SceneRenderer.PrepareExposureDomain";
  const bool wait = exposure_depth_ != 0U && label == "D3D12.FenceWait";
  if (!exposure && !wait) {
    return false;
  }
  if (stack_depth_ == stack_.size()) {
    invalid_ = true;
    return false;
  }
  auto record = kNoRecord;
  if (wait || exposure_depth_ == 0U) {
    if (records_.size() == records_.capacity()
      || label.size() >= Record {}.label.size()
      || label.find_first_of(",\r\n\"") != std::string_view::npos) {
      invalid_ = true;
    } else {
      record = records_.size();
      auto& value = records_.emplace_back();
      value.frame = frame_;
      value.wait = wait;
      std::memcpy(value.label.data(), label.data(), label.size());
      value.begin = Timestamp();
    }
  }
  stack_[stack_depth_++] = { record, wait };
  if (!wait) {
    ++exposure_depth_;
  }
  return true;
}

auto ExposureCpuTiming::OnScopeEnd() noexcept -> void
{
  const auto scope = stack_[--stack_depth_];
  if (scope.record != kNoRecord) {
    records_[scope.record].end = Timestamp();
  }
  if (!scope.wait) {
    --exposure_depth_;
  }
}

auto ExposureCpuTiming::Save(const std::filesystem::path& path) const
  -> nlohmann::json
{
  if (invalid_ || stack_depth_ != 0U || exposure_depth_ != 0U
    || records_.empty() || std::filesystem::exists(path)) {
    throw std::runtime_error("Invalid CPU timing capture or existing evidence");
  }
  std::ofstream output(path, std::ios::binary);
  output << "frame_seq,thread_id,kind,start_qpc,end_qpc,label\n";
  for (const auto& record : records_) {
    if (record.end < record.begin) {
      throw std::runtime_error("Incomplete CPU timing interval");
    }
    output << record.frame << ',' << thread_ << ','
           << (record.wait ? "fence_wait" : "exposure") << ',' << record.begin
           << ',' << record.end << ',' << record.label.data() << '\n';
  }
  output.close();
  if (!output.good()) {
    throw std::runtime_error("Could not write CPU timing evidence");
  }
  return { { "path", path.filename().string() },
    { "qpc_frequency", frequency_ }, { "process_id", process_ },
    { "thread_id", thread_ }, { "records", records_.size() },
    { "complete", true } };
}

} // namespace oxygen::vortex::testing::exposure
