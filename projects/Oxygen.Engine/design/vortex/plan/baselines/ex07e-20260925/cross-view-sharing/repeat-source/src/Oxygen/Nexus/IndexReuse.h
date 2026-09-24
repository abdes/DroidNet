//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Nexus/GenerationTracker.h>

namespace oxygen::nexus {

template <typename T>
concept IndexLike = std::integral<T> || (requires(T t) {
  { t.get() } -> std::convertible_to<std::size_t>;
} && std::equality_comparable<T>);

namespace detail {
  template <typename T> constexpr auto GetIndexValue(T idx) -> std::size_t
  {
    if constexpr (std::integral<T>) {
      return static_cast<std::size_t>(idx);
    } else {
      return static_cast<std::size_t>(idx.get());
    }
  }
} // namespace detail

//! Slot identity; generation zero never denotes a live allocation.
template <IndexLike IndexType> struct VersionedIndex {
  IndexType index {};
  bindless::Generation generation { 0 };
  auto operator<=>(const VersionedIndex&) const = default;
};

enum class RetireError { kOutOfRange, kStale, kAlreadyRetiring, kUnavailable };
enum class FinalizeDisposition {
  kReusable,
  kExhausted,
  kClosed,
  kAlreadyResolved
};

template <IndexLike IndexType> struct FinalizeResult {
  FinalizeDisposition disposition { FinalizeDisposition::kAlreadyResolved };
  std::optional<IndexType> index;
};

struct IndexReuseTelemetry {
  uint64_t allocate_calls { 0 };
  uint64_t release_calls { 0 };
  uint64_t stale_reject_count { 0 };
  uint64_t duplicate_reject_count { 0 };
  uint64_t reclaimed_count { 0 };
  uint64_t pending_count { 0 };
  uint64_t exhausted_slots { 0 };
  uint64_t abandoned_retirements { 0 };
};

namespace detail {
  enum class ReusePhase { kReusable, kLive, kRetiring, kUnavailable };
  enum class UnavailableReason { kNone, kExhausted, kAbandoned, kClosed };
  struct ReuseSlot {
    ReusePhase phase { ReusePhase::kReusable };
    uint32_t retiring_generation { 0 };
    UnavailableReason unavailable { UnavailableReason::kNone };
  };
  struct IndexReuseState {
    mutable std::mutex mutex;
    GenerationTracker generations;
    std::vector<ReuseSlot> slots;
    IndexReuseTelemetry telemetry;
    bool closed { false };
  };
} // namespace detail

template <IndexLike IndexType> class IndexReuse;
struct IndexReuseTestAccess;

//! Exclusive proof of a retired identity, resolved only by its completion
//! owner.
template <IndexLike IndexType> class RetirementTicket {
public:
  RetirementTicket(const RetirementTicket&) = delete;
  auto operator=(const RetirementTicket&) -> RetirementTicket& = delete;
  RetirementTicket(RetirementTicket&& other) noexcept
    : state_(std::move(other.state_))
    , handle_(other.handle_)
  {
  }
  auto operator=(RetirementTicket&& other) noexcept -> RetirementTicket&
  {
    if (this != &other) {
      Abandon();
      state_ = std::move(other.state_);
      handle_ = other.handle_;
    }
    return *this;
  }
  ~RetirementTicket() noexcept { Abandon(); }

  //! Complete internal state before the caller publishes the returned index.
  [[nodiscard]] auto Finalize() noexcept -> FinalizeResult<IndexType>
  {
    auto state = std::move(state_);
    if (!state) {
      return {};
    }
    std::lock_guard lock(state->mutex);
    auto& slot = state->slots[detail::GetIndexValue(handle_.index)];
    --state->telemetry.pending_count;
    ++state->telemetry.reclaimed_count;
    if (state->closed) {
      slot.phase = detail::ReusePhase::kUnavailable;
      slot.unavailable = detail::UnavailableReason::kClosed;
      return { FinalizeDisposition::kClosed, std::nullopt };
    }
    if (handle_.generation.get() == (std::numeric_limits<uint32_t>::max)()) {
      slot.phase = detail::ReusePhase::kUnavailable;
      slot.unavailable = detail::UnavailableReason::kExhausted;
      ++state->telemetry.exhausted_slots;
      return { FinalizeDisposition::kExhausted, std::nullopt };
    }
    slot.phase = detail::ReusePhase::kReusable;
    return { FinalizeDisposition::kReusable, handle_.index };
  }

private:
  friend class IndexReuse<IndexType>;
  RetirementTicket(std::shared_ptr<detail::IndexReuseState> state,
    VersionedIndex<IndexType> handle) noexcept
    : state_(std::move(state))
    , handle_(handle)
  {
  }
  auto Abandon() noexcept -> void
  {
    auto state = std::move(state_);
    if (!state) {
      return;
    }
    std::lock_guard lock(state->mutex);
    auto& slot = state->slots[detail::GetIndexValue(handle_.index)];
    slot.phase = detail::ReusePhase::kUnavailable;
    slot.unavailable = detail::UnavailableReason::kAbandoned;
    --state->telemetry.pending_count;
    ++state->telemetry.abandoned_retirements;
  }
  std::shared_ptr<detail::IndexReuseState> state_;
  VersionedIndex<IndexType> handle_;
};

//! Generation and retirement state independent of resources and completion
//! APIs.
template <IndexLike IndexType> class IndexReuse {
public:
  using TelemetrySnapshot = IndexReuseTelemetry;
  IndexReuse() = default;
  IndexReuse(const IndexReuse&) = delete;
  auto operator=(const IndexReuse&) -> IndexReuse& = delete;
  IndexReuse(IndexReuse&&) noexcept = default;
  auto operator=(IndexReuse&& other) noexcept -> IndexReuse&
  {
    if (this != &other) {
      Close();
      state_ = std::move(other.state_);
    }
    return *this;
  }
  ~IndexReuse() { Close(); }

  auto ActivateSlot(IndexType index) -> VersionedIndex<IndexType>
  {
    const auto raw = detail::GetIndexValue(index);
    constexpr auto limit = (std::numeric_limits<uint32_t>::max)();
    if (raw >= limit) {
      throw std::out_of_range("Invalid reusable slot index");
    }
    if (!state_) {
      throw std::logic_error("Moved-from index reuse core");
    }
    std::lock_guard lock(state_->mutex);
    if (state_->closed) {
      throw std::logic_error("Index reuse core is closed");
    }
    if (raw >= state_->slots.size()) {
      const auto capacity = (std::min)(std::size_t { limit },
        (std::max)({ std::size_t { 64 }, raw + 1, state_->slots.size() * 2 }));
      state_->slots.reserve(capacity);
      state_->generations.Resize(
        bindless::Capacity { static_cast<uint32_t>(capacity) });
      state_->slots.resize(capacity);
    }
    auto& slot = state_->slots[raw];
    if (slot.phase != detail::ReusePhase::kReusable) {
      throw std::logic_error("Slot is not reusable");
    }
    const auto generation = state_->generations.Load(
      bindless::HeapIndex { static_cast<uint32_t>(raw) });
    slot.phase = detail::ReusePhase::kLive;
    ++state_->telemetry.allocate_calls;
    return { index, generation };
  }

  [[nodiscard]] auto IsHandleCurrent(
    VersionedIndex<IndexType> handle) const noexcept -> bool
  {
    if (!state_) {
      return false;
    }
    std::lock_guard lock(state_->mutex);
    const auto raw = detail::GetIndexValue(handle.index);
    return !state_->closed && raw < state_->slots.size()
      && state_->slots[raw].phase == detail::ReusePhase::kLive
      && state_->generations.Load(
           bindless::HeapIndex { static_cast<uint32_t>(raw) })
      == handle.generation;
  }

  [[nodiscard]] auto TryRetire(VersionedIndex<IndexType> handle) noexcept
    -> std::expected<RetirementTicket<IndexType>, RetireError>
  {
    if (!state_) {
      return std::unexpected(RetireError::kUnavailable);
    }
    std::lock_guard lock(state_->mutex);
    ++state_->telemetry.release_calls;
    const auto reject = [&](RetireError error)
      -> std::expected<RetirementTicket<IndexType>, RetireError> {
      if (error == RetireError::kAlreadyRetiring) {
        ++state_->telemetry.duplicate_reject_count;
      } else {
        ++state_->telemetry.stale_reject_count;
      }
      return std::unexpected(error);
    };
    const auto raw = detail::GetIndexValue(handle.index);
    if (raw >= state_->slots.size()) {
      return reject(RetireError::kOutOfRange);
    }
    auto& slot = state_->slots[raw];
    if (slot.phase == detail::ReusePhase::kRetiring) {
      return reject(slot.retiring_generation == handle.generation.get()
          ? RetireError::kAlreadyRetiring
          : RetireError::kStale);
    }
    if (slot.phase == detail::ReusePhase::kUnavailable) {
      return reject(RetireError::kUnavailable);
    }
    const auto index = bindless::HeapIndex { static_cast<uint32_t>(raw) };
    if (slot.phase != detail::ReusePhase::kLive
      || state_->generations.Load(index) != handle.generation) {
      return reject(RetireError::kStale);
    }
    slot.phase = detail::ReusePhase::kRetiring;
    slot.retiring_generation = handle.generation.get();
    if (handle.generation.get() != (std::numeric_limits<uint32_t>::max)()) {
      state_->generations.Bump(index);
    }
    ++state_->telemetry.pending_count;
    return RetirementTicket<IndexType>(state_, handle);
  }

  auto Close() noexcept -> void
  {
    if (state_) {
      std::lock_guard lock(state_->mutex);
      state_->closed = true;
    }
  }
  [[nodiscard]] auto GetTelemetrySnapshot() const noexcept -> TelemetrySnapshot
  {
    if (!state_) {
      return {};
    }
    std::lock_guard lock(state_->mutex);
    return state_->telemetry;
  }

private:
  friend struct IndexReuseTestAccess;
  std::shared_ptr<detail::IndexReuseState> state_ {
    std::make_shared<detail::IndexReuseState>()
  };
};

} // namespace oxygen::nexus
