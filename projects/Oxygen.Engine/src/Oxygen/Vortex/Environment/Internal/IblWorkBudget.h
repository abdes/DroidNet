//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>

namespace oxygen::vortex::environment::internal {

//! Renderer-local IBL dispatch estimates, corrected by completed GPU samples.
//! All methods run on the renderer thread; feedback owns no GPU resources.
class IblWorkBudget final : public vortex::internal::GpuTimelineSink {
public:
  static constexpr double kFrameBudgetMs = 0.30;
  static constexpr double kSnapshotReserveMs = 0.10;
  struct Selection {
    std::uint32_t count {};
    double predicted_ms {};
  };

  OXGN_VRTX_API IblWorkBudget();
  IblWorkBudget(const IblWorkBudget&) = delete;
  auto operator=(const IblWorkBudget&) -> IblWorkBudget& = delete;
  [[nodiscard]] OXGN_VRTX_API static auto TimingLabel(
    const char* shader) noexcept -> const char*;
  [[nodiscard]] OXGN_VRTX_API auto Predict(const IblGpuDispatch& work) const
    -> double;
  [[nodiscard]] OXGN_VRTX_API auto Select(std::span<const IblGpuDispatch> work,
    double allowance_ms) const -> Selection;
  OXGN_VRTX_API auto Record(frame::SequenceNumber sequence,
    std::span<const IblGpuDispatch> work) noexcept -> void;
  OXGN_VRTX_API auto Reject(frame::SequenceNumber sequence) noexcept -> void;
  OXGN_VRTX_API auto ConsumeFrame(
    const vortex::internal::GpuTimelineFrame& frame) -> bool override;
  auto Close() noexcept -> void { active_ = false; }
  [[nodiscard]] auto SampleCount() const noexcept -> std::uint64_t
  {
    return samples_;
  }

private:
  static constexpr std::size_t kKinds = 13U;
  static constexpr std::size_t kPendingFrames
    = frame::kFramesInFlight.get() + 1U;
  static constexpr std::size_t kMaximumSamplesPerFrame = 4096U;
  struct Estimate {
    double overhead_ms {};
    double work_ms {};
  };
  struct Sample {
    std::uint64_t units {};
    std::size_t kind {};
    std::uint32_t dispatches {};
  };
  struct PendingFrame {
    std::uint64_t sequence {};
    std::size_t count {};
    bool rejected {};
    bool occupied {};
    std::array<Sample, kMaximumSamplesPerFrame> samples;
  };
  std::array<Estimate, kKinds> estimates_;
  std::array<PendingFrame, kPendingFrames> pending_ {};
  std::uint64_t samples_ {};
  bool active_ { true };
};

} // namespace oxygen::vortex::environment::internal
