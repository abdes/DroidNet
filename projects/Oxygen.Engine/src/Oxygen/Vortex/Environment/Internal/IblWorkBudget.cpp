//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include <Oxygen/Vortex/Environment/Internal/IblWorkBudget.h>

namespace oxygen::vortex::environment::internal {
namespace {
  constexpr auto kShaders
    = std::array<std::string_view, 13> { "IblInitializeCS", "IblPrepareCS",
        "IblCapturePrepareCS", "IblRangeCS", "IblNormalizeCS", "IblMipCS",
        "IblShCS", "IblShReduceCS", "IblPrefilterCS", "IblNarrowCS",
        "IblPrecisionRangeCS", "IblPrecisionReduceCS", "IblCompleteCS" };
  // Reference work at 128-face resolution. Seeds use retained stage attribution
  // and native batch measurements; completed samples correct them per device.
  constexpr auto kReferenceUnits = std::array<double, 13> { 1, 98304, 98304,
    1536, 98304, 32766, 98304, 15360, 4456320, 262140, 264192, 4128, 1 };
  constexpr auto kWorkMs = std::array<double, 13> { 0, 0.021, 0.028, 0.012,
    0.016, 0.026, 0.039, 0.033, 0.250, 0.060, 0.077, 0.016, 0 };
  constexpr auto kTimingLabels
    = std::array<const char*, 13> { "IBL.Batch.IblInitializeCS",
        "IBL.Batch.IblPrepareCS", "IBL.Batch.IblCapturePrepareCS",
        "IBL.Batch.IblRangeCS", "IBL.Batch.IblNormalizeCS",
        "IBL.Batch.IblMipCS", "IBL.Batch.IblShCS", "IBL.Batch.IblShReduceCS",
        "IBL.Batch.IblPrefilterCS", "IBL.Batch.IblNarrowCS",
        "IBL.Batch.IblPrecisionRangeCS", "IBL.Batch.IblPrecisionReduceCS",
        "IBL.Batch.IblCompleteCS" };

  auto Kind(std::string_view shader) -> std::size_t
  {
    const auto found = std::ranges::find(kShaders, shader);
    return static_cast<std::size_t>(found - kShaders.begin());
  }
  auto ScopeKind(std::string_view name) -> std::size_t
  {
    constexpr auto prefix = std::string_view("IBL.Batch.");
    return name.starts_with(prefix) ? Kind(name.substr(prefix.size()))
                                    : kShaders.size();
  }
}

auto IblWorkBudget::TimingLabel(const char* shader) noexcept -> const char*
{
  const auto kind = shader ? Kind(shader) : kShaders.size();
  return kind < kTimingLabels.size() ? kTimingLabels[kind]
                                     : "IBL.Batch.Unknown";
}

IblWorkBudget::IblWorkBudget()
{
  for (std::size_t kind = 0; kind < estimates_.size(); ++kind)
    estimates_[kind] = { kind == 0U || kind == 12U               ? 0.006
        : kind == 3U || kind == 7U || kind == 10U || kind == 11U ? 0.004
                                                                 : 0.001,
      kWorkMs[kind] };
}

auto IblWorkBudget::Predict(const IblGpuDispatch& work) const -> double
{
  const auto kind = work.shader ? Kind(work.shader) : kKinds;
  if (kind == kKinds)
    return kFrameBudgetMs;
  const auto& estimate = estimates_[kind];
  return estimate.overhead_ms
    + estimate.work_ms * static_cast<double>(work.work_units)
    / kReferenceUnits[kind];
}

auto IblWorkBudget::Select(const std::span<const IblGpuDispatch> work,
  const double allowance_ms) const -> Selection
{
  auto selection = Selection {};
  if (!std::isfinite(allowance_ms) || allowance_ms <= 0.0)
    return selection;
  for (const auto& step : work) {
    const auto cost = Predict(step);
    if (selection.count != 0U && selection.predicted_ms + cost > allowance_ms)
      break;
    // A single oversized dispatch still progresses. Its measured cost remains
    // visible to qualification; splitting that shader is an optimization task.
    ++selection.count;
    selection.predicted_ms += cost;
    if (selection.predicted_ms >= allowance_ms)
      break;
  }
  return selection;
}

auto IblWorkBudget::Record(const frame::SequenceNumber sequence,
  const std::span<const IblGpuDispatch> work) noexcept -> void
{
  auto& pending = pending_[sequence.get() % kPendingFrames];
  if (!pending.occupied || pending.sequence != sequence.get()) {
    pending.sequence = sequence.get();
    pending.count = 0U;
    pending.rejected = false;
    pending.occupied = true;
  }
  for (std::size_t index = 0U; index < work.size(); ++index) {
    const auto& step = work[index];
    const auto kind = step.shader ? Kind(step.shader) : kKinds;
    if (kind == kKinds || step.work_units == 0U) {
      pending.rejected = true;
      return;
    }
    if (index != 0U && work[index - 1U].CanShareBatchWith(step)) {
      auto& sample = pending.samples[pending.count - 1U];
      sample.units += step.work_units;
      ++sample.dispatches;
    } else {
      if (pending.count == kMaximumSamplesPerFrame) {
        pending.rejected = true;
        return;
      }
      pending.samples[pending.count++] = { step.work_units, kind, 1U };
    }
  }
}

auto IblWorkBudget::Reject(const frame::SequenceNumber sequence) noexcept
  -> void
{
  auto& pending = pending_[sequence.get() % kPendingFrames];
  if (pending.occupied && pending.sequence == sequence.get())
    pending.rejected = true;
}

auto IblWorkBudget::ConsumeFrame(
  const vortex::internal::GpuTimelineFrame& frame) -> bool
{
  if (!active_)
    return false;
  auto& pending = pending_[frame.frame_sequence % kPendingFrames];
  if (!pending.occupied || pending.sequence != frame.frame_sequence
    || pending.count == 0U)
    return true;
  pending.occupied = false;
  if (pending.rejected || !frame.profiling_enabled || frame.overflowed)
    return true;
  // Validate the whole correspondence before training any coefficient. A failed
  // or discarded recording can leave timestamp scopes without submitted work.
  std::size_t index = 0U;
  for (const auto& scope : frame.scopes) {
    const auto kind = ScopeKind(scope.display_name);
    if (kind == kKinds)
      continue;
    if (index == pending.count || pending.samples[index].kind != kind
      || !scope.valid || !std::isfinite(scope.duration_ms)
      || scope.duration_ms < 0.0F)
      return true;
    ++index;
  }
  if (index != pending.count)
    return true;
  index = 0U;
  for (const auto& scope : frame.scopes) {
    const auto kind = ScopeKind(scope.display_name);
    if (kind == kKinds)
      continue;
    const auto sample = pending.samples[index++];
    if (scope.duration_ms == 0.0F)
      continue;
    auto& estimate = estimates_[kind];
    const double x = static_cast<double>(sample.units) / kReferenceUnits[kind];
    const double launches = sample.dispatches;
    const double error = scope.duration_ms
      - (estimate.overhead_ms * launches + estimate.work_ms * x);
    // Normalized gradient update separates launch and work cost. A small step
    // limits how much one delayed GPU batch can throttle subsequent candidates.
    const double correction = 0.05 * error / (launches * launches + x * x);
    estimate.overhead_ms
      = std::max(0.0002, estimate.overhead_ms + correction * launches);
    estimate.work_ms = std::max(0.0, estimate.work_ms + correction * x);
    ++samples_;
  }
  return true;
}
} // namespace oxygen::vortex::environment::internal
