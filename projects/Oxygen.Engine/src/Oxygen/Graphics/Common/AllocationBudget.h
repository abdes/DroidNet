//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

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
  [[nodiscard]] OXGN_GFX_API auto Snapshot() const -> AllocationBudgetSnapshot;
  //! Backend rejection uses the same bounded, caller-readable failure record.
  OXGN_GFX_API auto RecordRejection(SizeBytes requested, SizeBytes available)
    -> void;

private:
  std::shared_ptr<detail::AllocationBudgetState> state_;
};

//! Resource factories throw this before exposing an unadmitted allocation.
class AllocationBudgetExceeded final : public std::runtime_error {
public:
  AllocationBudgetExceeded()
    : std::runtime_error("GPU resource allocation exceeds available budget")
  {
  }
};

} // namespace oxygen::graphics
