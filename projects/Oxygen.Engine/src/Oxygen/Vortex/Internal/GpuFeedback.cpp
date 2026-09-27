//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/RecordingUseBatch.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Vortex/Internal/GpuFeedback.h>

namespace oxygen::vortex::internal {
namespace {
  //! GPU batches retain only this signal, never the pool or Graphics facade.
  struct FeedbackUse {
    std::atomic<bool> released { true };
    std::atomic<bool> accepted { false };
    std::atomic<bool> failed { false };
  };
  constexpr auto kFeedbackUseTag = std::uint64_t { 0x464545444241434bULL };
  constexpr auto kFeedbackHooks = graphics::OpaqueUseHooks {
    .prepare = [](void* context, graphics::QueueIdentity) -> void {
      auto& use = *static_cast<FeedbackUse*>(context);
      use.released.store(false, std::memory_order_release);
    },
    .valid = nullptr,
    .submitted = [](void* context, graphics::QueueIdentity,
                   const graphics::SubmissionResult& result) noexcept -> void {
      auto& use = *static_cast<FeedbackUse*>(context);
      const bool accepted
        = result.outcome == graphics::SubmissionOutcome::kSubmitted
        && result.receipt.has_value();
      use.accepted.store(accepted, std::memory_order_release);
      use.failed.store(!accepted, std::memory_order_release);
    },
    .released = [](void* context, graphics::QueueIdentity,
                  graphics::SubmissionOutcome outcome,
                  graphics::UseReleaseReason reason) noexcept -> void {
      auto& use = *static_cast<FeedbackUse*>(context);
      if (outcome != graphics::SubmissionOutcome::kSubmitted
        || reason == graphics::UseReleaseReason::kDeviceLost) {
        use.failed.store(true, std::memory_order_release);
      }
      use.released.store(true, std::memory_order_release);
    },
    .requires_completion = true,
  };
} // namespace

struct GpuFeedbackSlot {
  std::shared_ptr<FeedbackUse> use { std::make_shared<FeedbackUse>() };
  std::shared_ptr<graphics::GpuBufferReadback> readback;
  bool owned { false };
  bool recorded { false };
  bool copied { false };
  bool committed { false };
  std::optional<graphics::ReadbackError> failure;
};

struct GpuFeedbackState {
  // Destruction releases readbacks before their manager's Graphics owner.
  std::shared_ptr<Graphics> graphics;
  std::string debug_name;
  std::vector<GpuFeedbackSlot> slots;
  std::size_t created_readbacks { 0 };

  auto Recycle(GpuFeedbackSlot& slot) noexcept -> void
  {
    if (slot.owned || !slot.use->released.load(std::memory_order_acquire)) {
      return;
    }
    if (slot.readback && slot.recorded) {
      bool reusable = false;
      try {
        reusable = slot.copied && !slot.failure
          && !slot.use->failed.load(std::memory_order_acquire)
          && slot.readback->ResetForReuse().has_value();
      } catch (const std::exception&) {
        reusable = false;
      }
      if (!reusable) {
        slot.readback.reset();
      }
    }
    slot.recorded = false;
    slot.copied = false;
    slot.committed = false;
    slot.failure.reset();
  }

  auto Release(GpuFeedbackSlot& slot) noexcept -> void
  {
    slot.owned = false;
    Recycle(slot);
  }
};

GpuFeedbackReservation::GpuFeedbackReservation(
  std::shared_ptr<GpuFeedbackState> state, GpuFeedbackSlot& slot) noexcept
  : state_(std::move(state))
  , slot_(std::addressof(slot))
{
}
GpuFeedbackReservation::~GpuFeedbackReservation() { Reset(); }
GpuFeedbackReservation::GpuFeedbackReservation(
  GpuFeedbackReservation&& other) noexcept
  : state_(std::move(other.state_))
  , slot_(std::exchange(other.slot_, {}))
{
}
auto GpuFeedbackReservation::operator=(GpuFeedbackReservation&& other) noexcept
  -> GpuFeedbackReservation&
{
  if (this != &other) {
    Reset();
    state_ = std::move(other.state_);
    slot_ = std::exchange(other.slot_, {});
  }
  return *this;
}
auto GpuFeedbackReservation::Reset() noexcept -> void
{
  if (slot_ != nullptr) {
    state_->Release(*slot_);
    slot_.reset();
  }
  state_.reset();
}

auto GpuFeedbackReservation::EnqueueCopy(graphics::CommandRecorder& recorder,
  const graphics::Buffer& source, const graphics::BufferRange range)
  -> Result<void, graphics::ReadbackError>
try {
  if (slot_ == nullptr || slot_->recorded) {
    return Err(graphics::ReadbackError::kInvalidArgument);
  }
  // Charge capacity even if EnqueueCopy fails after recording a partial copy.
  recorder.RetainOpaqueUse(
    slot_->use, kFeedbackUseTag, slot_->use.get(), kFeedbackHooks);
  slot_->recorded = true;
  const auto copied = slot_->readback->EnqueueCopy(recorder, source, range);
  if (!copied) {
    return Err(copied.error());
  }
  slot_->copied = true;
  return {};
} catch (const std::exception&) {
  return Err(graphics::ReadbackError::kBackendFailure);
}

auto GpuFeedbackReservation::Commit() noexcept
  -> Result<void, graphics::ReadbackError>
{
  if (slot_ == nullptr || !slot_->copied) {
    return Err(graphics::ReadbackError::kInvalidArgument);
  }
  if (slot_->use->failed.load(std::memory_order_acquire)) {
    return Err(graphics::ReadbackError::kBackendFailure);
  }
  if (!slot_->use->accepted.load(std::memory_order_acquire)) {
    return Err(graphics::ReadbackError::kNotReady);
  }
  slot_->committed = true;
  return {};
}

auto GpuFeedbackReservation::IsReady() const
  -> Result<bool, graphics::ReadbackError>
try {
  if (slot_ == nullptr || !slot_->committed) {
    return Err(graphics::ReadbackError::kNotReady);
  }
  if (slot_->failure) {
    return Err(*slot_->failure);
  }
  if (slot_->use->failed.load(std::memory_order_acquire)) {
    return Err(graphics::ReadbackError::kBackendFailure);
  }
  const auto ready = slot_->readback->IsReady();
  if (!ready) {
    slot_->failure = ready.error();
    return Err(ready.error());
  }
  return Ok(*ready);
} catch (const std::exception&) {
  if (slot_ != nullptr) {
    slot_->failure = graphics::ReadbackError::kBackendFailure;
  }
  return Err(graphics::ReadbackError::kBackendFailure);
}

auto GpuFeedbackReservation::ReadBytes(std::span<std::byte> destination,
  const FeedbackPayloadSize size) -> Result<bool, graphics::ReadbackError>
try {
  const auto ready = IsReady();
  if (!ready) {
    return Err(ready.error());
  }
  if (!*ready) {
    return Ok(false);
  }
  const auto mapped = slot_->readback->TryMap();
  if (!mapped) {
    slot_->failure = mapped.error();
    return Err(mapped.error());
  }
  const auto bytes = mapped->Bytes();
  if (bytes.size() < destination.size()
    || (size == FeedbackPayloadSize::kExact
      && bytes.size() != destination.size())) {
    slot_->failure = graphics::ReadbackError::kInvalidArgument;
    return Err(graphics::ReadbackError::kInvalidArgument);
  }
  std::memcpy(destination.data(), bytes.data(), destination.size());
  return Ok(true);
} catch (const std::exception&) {
  if (slot_ != nullptr) {
    slot_->failure = graphics::ReadbackError::kBackendFailure;
  }
  return Err(graphics::ReadbackError::kBackendFailure);
}

GpuFeedbackPool::GpuFeedbackPool(std::shared_ptr<Graphics> graphics,
  const std::size_t capacity, const std::string_view debug_name)
{
  if (!graphics || capacity == 0) {
    throw std::invalid_argument(
      "Feedback requires Graphics and positive capacity");
  }
  state_ = std::make_shared<GpuFeedbackState>();
  state_->graphics = std::move(graphics);
  state_->debug_name = debug_name;
  state_->slots.resize(capacity);
}
GpuFeedbackPool::~GpuFeedbackPool() = default;

auto GpuFeedbackPool::Reserve() noexcept
  -> Result<GpuFeedbackReservation, FeedbackReserveError>
try {
  state_->graphics->PollCompletedUses();
  for (auto& slot : state_->slots) {
    state_->Recycle(slot);
    if (slot.owned || slot.recorded) {
      continue;
    }
    if (!slot.readback) {
      const auto manager = state_->graphics->GetReadbackManager();
      if (!manager) {
        return Err(FeedbackReserveError::kUnavailable);
      }
      slot.readback = manager->CreateBufferReadback(state_->debug_name);
      if (!slot.readback) {
        return Err(FeedbackReserveError::kAllocationFailed);
      }
      ++state_->created_readbacks;
    }
    slot.use->accepted.store(false, std::memory_order_release);
    slot.use->failed.store(false, std::memory_order_release);
    slot.owned = true;
    return Ok(GpuFeedbackReservation(state_, slot));
  }
  return Err(FeedbackReserveError::kBusy);
} catch (const std::bad_alloc&) {
  return Err(FeedbackReserveError::kAllocationFailed);
} catch (const std::exception&) {
  return Err(FeedbackReserveError::kBackendFailure);
}
auto GpuFeedbackPool::InspectStats() const noexcept -> Stats
{
  return {
    .capacity = state_->slots.size(),
    .occupied = static_cast<std::size_t>(std::ranges::count_if(state_->slots,
      [](const GpuFeedbackSlot& slot) -> bool {
        return slot.owned || slot.recorded;
      })),
    .created_readbacks = state_->created_readbacks,
  };
}
} // namespace oxygen::vortex::internal
