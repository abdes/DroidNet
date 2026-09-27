//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Nexus/IndexReuse.h>

namespace oxygen::nexus {

//! Owner/use accounting under the caller's lock. Graphics decides when uses
//! end.
template <IndexLike IndexType> class RetirementState final {
public:
  RetirementState() = default;
  ~RetirementState() = default;
  OXYGEN_MAKE_NON_COPYABLE(RetirementState)
  OXYGEN_MAKE_NON_MOVABLE(RetirementState)

  //! Call only after successful IndexReuse activation; zero owners permits
  //! construction before the first owner is installed.
  [[nodiscard]] auto Activate(const std::size_t owners = 0) noexcept -> bool
  {
    if (phase_ != Phase::kInactive) {
      return false;
    }
    owners_ = owners;
    phase_ = Phase::kOpen;
    return true;
  }

  [[nodiscard]] auto AcquireOwner() noexcept -> bool
  {
    return phase_ == Phase::kOpen && Increment(owners_);
  }

  [[nodiscard]] auto AcquireUse() noexcept -> bool
  {
    return CanAcquireUse() && Increment(uses_);
  }

  [[nodiscard]] auto AcquireRetainedPin() noexcept -> bool
  {
    return CanAcquireUse() && Increment(pins_);
  }

  [[nodiscard]] auto ReleaseOwner() noexcept -> bool
  {
    if (!Decrement(owners_)) {
      return false;
    }
    if (owners_ == 0) {
      phase_ = Phase::kRetiring;
    }
    return true;
  }

  [[nodiscard]] auto ReleaseUse() noexcept -> bool { return Decrement(uses_); }
  [[nodiscard]] auto ReleaseRetainedPin() noexcept -> bool
  {
    return Decrement(pins_);
  }

  //! One-time request to obtain a ticket from IndexReuse. Also seals an
  //! activated, ownerless construction during rollback.
  [[nodiscard]] auto BeginRetirement() noexcept -> bool
  {
    if (phase_ == Phase::kInactive || phase_ == Phase::kResolved || owners_ != 0
      || retirement_requested_) {
      return false;
    }
    phase_ = Phase::kRetiring;
    retirement_requested_ = true;
    return true;
  }

  //! Install the requested ticket. No ticket means the issuing pool is gone
  //! or rejected retirement; counts still drain, but no index is published.
  [[nodiscard]] auto SetRetirement(
    std::optional<RetirementTicket<IndexType>> ticket) noexcept -> bool
  {
    if (!retirement_requested_ || ticket_ready_) {
      return false;
    }
    ticket_ = std::move(ticket);
    ticket_ready_ = true;
    return true;
  }

  //! Releases ordinary admission once, independently of retained captures.
  [[nodiscard]] auto TakeOrdinaryDrained() noexcept -> bool
  {
    if ((phase_ != Phase::kRetiring && phase_ != Phase::kResolved)
      || owners_ != 0 || uses_ != 0 || ordinary_drained_) {
      return false;
    }
    ordinary_drained_ = true;
    return true;
  }

  //! Only a returned result can authorize free-index publication. Repeated or
  //! premature calls return no result; retirement never allocates or waits.
  [[nodiscard]] auto Finalize() noexcept
    -> std::optional<FinalizeResult<IndexType>>
  {
    if (phase_ != Phase::kRetiring || !ticket_ready_ || owners_ != 0
      || uses_ != 0 || pins_ != 0) {
      return std::nullopt;
    }
    phase_ = Phase::kResolved;
    const auto result = ticket_ ? ticket_->Finalize()
                                : FinalizeResult<IndexType> {
                                    FinalizeDisposition::kClosed,
                                    std::nullopt,
                                  };
    ticket_.reset();
    return result;
  }

  [[nodiscard]] auto IsActive() const noexcept -> bool
  {
    return phase_ != Phase::kInactive;
  }
  [[nodiscard]] auto IsResolved() const noexcept -> bool
  {
    return phase_ == Phase::kResolved;
  }
  [[nodiscard]] auto CanAcquireUse() const noexcept -> bool
  {
    return phase_ == Phase::kOpen && owners_ != 0;
  }
  [[nodiscard]] auto OwnerCount() const noexcept -> std::size_t
  {
    return owners_;
  }
  [[nodiscard]] auto UseCount() const noexcept -> std::size_t { return uses_; }
  [[nodiscard]] auto RetainedPinCount() const noexcept -> std::size_t
  {
    return pins_;
  }

private:
  enum class Phase : std::uint8_t { kInactive, kOpen, kRetiring, kResolved };

  static auto Increment(std::size_t& count) noexcept -> bool
  {
    if (count == std::numeric_limits<std::size_t>::max()) {
      return false;
    }
    ++count;
    return true;
  }
  static auto Decrement(std::size_t& count) noexcept -> bool
  {
    if (count == 0) {
      return false;
    }
    --count;
    return true;
  }

  Phase phase_ { Phase::kInactive };
  std::size_t owners_ { 0 };
  std::size_t uses_ { 0 };
  std::size_t pins_ { 0 };
  std::optional<RetirementTicket<IndexType>> ticket_;
  bool retirement_requested_ { false };
  bool ticket_ready_ { false };
  bool ordinary_drained_ { false };
};

} // namespace oxygen::nexus
