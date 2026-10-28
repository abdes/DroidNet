//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/AllocationBudgetTag.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct AllocationBudgetLimits {
  SizeBytes total;
  SizeBytes compact_indices;
  SizeBytes driver_headroom;
};

struct AllocationBudgetSnapshot {
  AllocationBudgetLimits limits;
  SizeBytes allocated;
  SizeBytes compact_indices;
  SizeBytes peak_allocated;
  std::uint64_t rejected_requests {};
  SizeBytes last_requested;
  SizeBytes last_available;
};

namespace detail {
  struct AllocationBudgetState;
}

//! Move-only charge, retained until the backing allocation is actually freed.
class AllocationReservation final {
public:
  AllocationReservation() = default;
  OXGN_GFX_API ~AllocationReservation();
  AllocationReservation(const AllocationReservation&) = delete;
  auto operator=(const AllocationReservation&)
    -> AllocationReservation& = delete;
  OXGN_GFX_API AllocationReservation(AllocationReservation&& other) noexcept;
  OXGN_GFX_API auto operator=(AllocationReservation&& other) noexcept
    -> AllocationReservation&;
  OXGN_GFX_API auto Reset() noexcept -> void;
  //! Reconcile a conservative preflight with the allocator's actual size.
  OXGN_GFX_API auto ReduceTo(SizeBytes actual_bytes) -> void;
  [[nodiscard]] auto Size() const noexcept -> SizeBytes { return size_; }

private:
  friend class AllocationBudget;
  AllocationReservation(std::shared_ptr<detail::AllocationBudgetState> state,
    SizeBytes size, AllocationCategory category);
  std::shared_ptr<detail::AllocationBudgetState> state_;
  SizeBytes size_ { 0U };
  AllocationCategory category_ { AllocationCategory::kGeneral };
};

//! One allocator domain shared by all views and resource generations.
//! The compact-index ceiling is a subset of the total, never extra capacity.
class AllocationBudget final {
public:
  OXGN_GFX_API explicit AllocationBudget(AllocationBudgetLimits limits);
  [[nodiscard]] OXGN_GFX_API auto TryReserve(SizeBytes actual_bytes,
    AllocationCategory category = AllocationCategory::kGeneral)
    -> std::optional<AllocationReservation>;
  //! Reserve like TryReserve, for resource factories.
  //! @throw AllocationBudgetExceeded with the bytes still available when the
  //! request does not fit.
  [[nodiscard]] OXGN_GFX_API auto Reserve(SizeBytes actual_bytes,
    AllocationCategory category = AllocationCategory::kGeneral)
    -> AllocationReservation;
  [[nodiscard]] OXGN_GFX_API auto Snapshot() const -> AllocationBudgetSnapshot;
  //! Backend rejection uses the same bounded, caller-readable failure record.
  OXGN_GFX_API auto RecordRejection(SizeBytes requested, SizeBytes available)
    -> void;

private:
  //! Admit a request, or return the bytes still available.
  auto Admit(SizeBytes actual_bytes, AllocationCategory category)
    -> std::expected<AllocationReservation, SizeBytes>;

  std::shared_ptr<detail::AllocationBudgetState> state_;
};

//! Resource factories throw this before exposing an unadmitted allocation,
//! whether the budget's own limit or the driver's segment budget refused it.
class AllocationBudgetExceeded final : public std::runtime_error {
public:
  //! @param requested Bytes the refused allocation needs.
  //! @param available Bytes the budget could still admit.
  //! @param driver_headroom Reserve kept free below the driver's segment
  //! budget; zero when the budget's own limit refused the request.
  AllocationBudgetExceeded(SizeBytes requested, SizeBytes available,
    SizeBytes driver_headroom = SizeBytes { 0U })
    : std::runtime_error(Describe(requested, available, driver_headroom))
    , requested_(requested)
    , available_(available)
    , driver_headroom_(driver_headroom)
  {
  }

  [[nodiscard]] auto Requested() const noexcept -> SizeBytes
  {
    return requested_;
  }
  [[nodiscard]] auto Available() const noexcept -> SizeBytes
  {
    return available_;
  }
  [[nodiscard]] auto DriverHeadroom() const noexcept -> SizeBytes
  {
    return driver_headroom_;
  }

private:
  static auto Describe(SizeBytes requested, SizeBytes available,
    SizeBytes driver_headroom) -> std::string
  {
    auto text = "GPU allocation of " + to_string(requested)
      + " exceeds the budget: " + to_string(available) + " available";
    if (driver_headroom != SizeBytes { 0U }) {
      text += " after " + to_string(driver_headroom) + " of driver headroom";
    }
    return text;
  }

  SizeBytes requested_;
  SizeBytes available_;
  SizeBytes driver_headroom_;
};

} // namespace oxygen::graphics
