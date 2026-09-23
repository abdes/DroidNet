//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/AllocationBudgetTag.h>

namespace oxygen::graphics {
namespace detail {
  struct AllocationBudgetState {
    explicit AllocationBudgetState(AllocationBudgetLimits configuration)
      : limits(configuration)
    {
    }
    std::mutex mutex;
    AllocationBudgetLimits limits;
    std::uint64_t allocated { 0U };
    std::uint64_t compact_indices { 0U };
    std::uint64_t peak { 0U };
    std::uint64_t rejected { 0U };
    SizeBytes last_requested { 0U };
    SizeBytes last_available { 0U };
  };
} // namespace detail

AllocationReservation::AllocationReservation(
  std::shared_ptr<detail::AllocationBudgetState> state, SizeBytes size,
  AllocationCategory category)
  : state_(std::move(state))
  , size_(size)
  , category_(category)
{
}

AllocationReservation::~AllocationReservation() { Reset(); }

AllocationReservation::AllocationReservation(
  AllocationReservation&& other) noexcept
  : state_(std::move(other.state_))
  , size_(std::exchange(other.size_, SizeBytes { 0U }))
  , category_(other.category_)
{
}

auto AllocationReservation::operator=(AllocationReservation&& other) noexcept
  -> AllocationReservation&
{
  if (this != &other) {
    Reset();
    state_ = std::move(other.state_);
    size_ = std::exchange(other.size_, SizeBytes { 0U });
    category_ = other.category_;
  }
  return *this;
}

auto AllocationReservation::Reset() noexcept -> void
{
  if (state_) {
    {
      const auto lock = std::scoped_lock(state_->mutex);
      state_->allocated -= size_.get();
      if (category_ == AllocationCategory::kCompactIndices) {
        state_->compact_indices -= size_.get();
      }
    }
    state_.reset();
    size_ = SizeBytes { 0U };
  }
}

AllocationBudget::AllocationBudget(AllocationBudgetLimits limits)
  : state_(std::make_shared<detail::AllocationBudgetState>(limits))
{
}

auto AllocationReservation::ReduceTo(SizeBytes actual_bytes) -> void
{
  if (actual_bytes > size_) {
    throw std::invalid_argument(
      "Backend allocation exceeds its preflight size");
  }
  if (state_) {
    const auto lock = std::scoped_lock(state_->mutex);
    const auto difference = size_.get() - actual_bytes.get();
    state_->allocated -= difference;
    if (category_ == AllocationCategory::kCompactIndices) {
      state_->compact_indices -= difference;
    }
    size_ = actual_bytes;
  }
}

auto AllocationBudget::TryReserve(SizeBytes actual_bytes,
  AllocationCategory category) -> std::optional<AllocationReservation>
{
  const auto lock = std::scoped_lock(state_->mutex);
  auto available = state_->limits.total.get() - state_->allocated;
  if (category == AllocationCategory::kCompactIndices) {
    available = std::min(available,
      state_->limits.compact_indices.get() - state_->compact_indices);
  }
  if (actual_bytes.get() > available) {
    ++state_->rejected;
    state_->last_requested = actual_bytes;
    state_->last_available = SizeBytes { available };
    return std::nullopt;
  }
  state_->allocated += actual_bytes.get();
  if (category == AllocationCategory::kCompactIndices) {
    state_->compact_indices += actual_bytes.get();
  }
  state_->peak = std::max(state_->peak, state_->allocated);
  return AllocationReservation { state_, actual_bytes, category };
}

auto AllocationBudget::Snapshot() const -> AllocationBudgetSnapshot
{
  const auto lock = std::scoped_lock(state_->mutex);
  return {
    .limits = state_->limits,
    .allocated = SizeBytes { state_->allocated },
    .compact_indices = SizeBytes { state_->compact_indices },
    .peak_allocated = SizeBytes { state_->peak },
    .rejected_requests = state_->rejected,
    .last_requested = state_->last_requested,
    .last_available = state_->last_available,
  };
}

auto AllocationBudget::RecordRejection(SizeBytes requested, SizeBytes available)
  -> void
{
  const auto lock = std::scoped_lock(state_->mutex);
  ++state_->rejected;
  state_->last_requested = requested;
  state_->last_available = available;
}

} // namespace oxygen::graphics
