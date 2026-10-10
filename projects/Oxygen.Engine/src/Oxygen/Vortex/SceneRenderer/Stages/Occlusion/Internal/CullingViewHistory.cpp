//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::occlusion::internal {

namespace {

  constexpr auto kSlotStride
    = static_cast<std::uint32_t>(sizeof(std::uint32_t));
  //! The smallest history, in slots.
  constexpr std::uint32_t kMinHistoryCapacity = 64U;

  auto TrackFromKnownOrCommon(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kCommon, true);
  }

} // namespace

CullingViewHistory::CullingViewHistory(std::string debug_name)
  : debug_name_(std::move(debug_name))
{
}

CullingViewHistory::~CullingViewHistory() = default;

auto CullingViewHistory::Prepare(graphics::CommandRecorder& recorder,
  const std::shared_ptr<Graphics>& gfx,
  upload::TransientStructuredBuffer& slot_words,
  const std::span<const PreparedSceneFrame::DrawSource> draws,
  const std::uint32_t draw_count, const bool reset)
  -> std::optional<DrawCullHistory>
{
  const auto valid = written_ && !reset;
  written_ = false;

  const auto assignments = slots_.Update(draws);
  slot_words_.assign(draw_count, kNoHistorySlot);
  for (const auto& [word, assignment] :
    std::views::zip(slot_words_, assignments)) {
    word = assignment.ToSlotWord();
  }
  const auto allocation = slot_words.Allocate(draw_count);
  if (!allocation.has_value()
    || !allocation->TryWriteRange(
      std::span<const std::uint32_t>(slot_words_))) {
    LOG_F(ERROR, "{}: failed to upload the history slots", debug_name_);
    return std::nullopt;
  }
  if (!EnsureCapacity(recorder, gfx)) {
    return std::nullopt;
  }
  return DrawCullHistory {
    .slots_srv = allocation->srv,
    .history = observer_ptr<const graphics::Buffer> { history_->GetBuffer() },
    .history_uav = history_->GetUav(),
    .valid = valid,
    .stats = nullptr,
    .stats_uav = kInvalidShaderVisibleIndex,
  };
}

auto CullingViewHistory::EnsureCapacity(graphics::CommandRecorder& recorder,
  const std::shared_ptr<Graphics>& gfx) -> bool
{
  const auto capacity = slots_.Capacity();
  if (history_ != nullptr && history_->GetCapacity() >= capacity) {
    return true;
  }
  auto grown = std::make_unique<vortex::internal::StructuredGpuBuffer>(
    debug_name_ + ".History", kSlotStride);
  if (!grown->Ensure(
        gfx, std::bit_ceil((std::max)(capacity, kMinHistoryCapacity)))) {
    return false;
  }
  if (history_ != nullptr) {
    const auto& source = *history_->GetBuffer();
    auto& target = *grown->GetBuffer();
    TrackFromKnownOrCommon(recorder, source);
    TrackFromKnownOrCommon(recorder, target);
    recorder.RequireResourceState(
      source, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(target, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(target, 0U, source, 0U,
      static_cast<std::size_t>(history_->GetCapacity()) * kSlotStride);
  }
  history_ = std::move(grown);
  return true;
}

} // namespace oxygen::vortex::occlusion::internal
