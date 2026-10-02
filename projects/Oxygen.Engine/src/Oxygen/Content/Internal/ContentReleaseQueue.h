//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/RefCountedEviction.h>

namespace oxygen::content::internal {

//! Owner-thread cache returns with allocation-free, any-thread final release.
class ContentReleaseQueue final
  : public std::enable_shared_from_this<ContentReleaseQueue> {
public:
  using Cache = AnyCache<uint64_t, RefCountedEviction<uint64_t>>;

  //! A custom control allocator must outlive every returned pointer.
  explicit ContentReleaseQueue(std::pmr::memory_resource& control_allocator
    = *std::pmr::get_default_resource())
    : control_allocator_(&control_allocator)
  {
  }
  ~ContentReleaseQueue();
  OXYGEN_MAKE_NON_COPYABLE(ContentReleaseQueue)
  OXYGEN_MAKE_NON_MOVABLE(ContentReleaseQueue)

  //! Construct one independent request owner; pointer copies share its usage.
  template <IsTyped T>
  [[nodiscard]] auto Own(Cache& cache, Cache::Acquisition<T> acquired)
    -> std::shared_ptr<T>
  {
    return Own(cache, std::move(acquired.value),
      std::optional<Cache::UsageTicket> { std::move(acquired.ticket) });
  }

  //! An uncached result owns its payload without charging another incarnation.
  template <typename T>
  [[nodiscard]] auto Own(Cache& cache, std::shared_ptr<T> value,
    std::optional<Cache::UsageTicket> usage = {}) -> std::shared_ptr<T>
  {
    auto* const pointer = value.get();
    auto owner = MakeOwner(cache, std::move(value), std::move(usage));
    return std::shared_ptr<T>(std::move(owner), pointer);
  }

  [[nodiscard]] auto Pin(Cache& cache, Cache::UsageTicket usage)
    -> std::shared_ptr<void>;

  //! Owner-thread only. Finishes an older detached batch before newer arrivals.
  [[nodiscard]] auto Drain(Cache& cache, std::size_t limit) -> std::size_t;

  //! Owner-thread only. Close before clearing/destroying the cache. Remaining
  //! records then dispose CPU storage without accessing cache accounting.
  auto Close() noexcept -> void;

  [[nodiscard]] auto IsClosed() const noexcept -> bool { return closed_; }

  auto RequireOpen() const -> void
  {
    if (closed_) {
      throw OperationCancelledException("Content release queue is closed");
    }
  }

  [[nodiscard]] auto HasPending() const noexcept -> bool;

private:
  struct Record final {
    Record* next = nullptr;
    std::shared_ptr<const void> value {};
    std::optional<Cache::UsageTicket> usage {};
  };

  auto MakeOwner(Cache& cache, std::shared_ptr<const void> value,
    std::optional<Cache::UsageTicket> usage) -> std::shared_ptr<Record>;

  struct ReturnRecord final {
    std::weak_ptr<ContentReleaseQueue> queue {};
    auto operator()(Record* record) const noexcept -> void;
  };

  auto Enqueue(Record* record) noexcept -> void;
  [[nodiscard]] auto Pop() noexcept -> std::unique_ptr<Record>;

  Record closed_sentinel_ {};
  std::atomic<Record*> incoming_ { nullptr };
  Record* pending_ = nullptr;
  Record* closing_batch_ = nullptr;
  bool closed_ = false;
  bool draining_ = false;
  std::pmr::memory_resource* control_allocator_;
};

} // namespace oxygen::content::internal
