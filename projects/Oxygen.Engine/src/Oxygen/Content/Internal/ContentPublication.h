//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>

namespace oxygen::content::internal {

//! Bare decoded data and its exact optional cache incarnation; owns no
//! checkout.
struct ContentPublication final {
  std::shared_ptr<void> value {};
  std::optional<ContentReleaseQueue::Cache::EntryReference> entry {};
  TypeId type { kInvalidTypeId };

  template <IsTyped T>
  [[nodiscard]] auto Acquire(ContentReleaseQueue::Cache& cache,
    ContentReleaseQueue& releases, const CheckoutOwner owner) const
    -> std::shared_ptr<T>
  {
    releases.RequireOpen();
    if (!value || type != T::ClassTypeId()) {
      return {};
    }
    if (entry) {
      if (auto acquired = cache.Acquire<T>(*entry, owner)) {
        return releases.Own(cache, std::move(*acquired));
      }
    }
    return releases.Own(cache, std::static_pointer_cast<T>(value));
  }
};

//! One accepted request: immutable publication metadata and its owning control.
struct ContentAcquisition final {
  ContentPublication publication {};
  std::shared_ptr<void> owner {};

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return static_cast<bool>(owner);
  }

  template <IsTyped T>
  [[nodiscard]] static auto FromCache(ContentReleaseQueue::Cache& cache,
    ContentReleaseQueue& releases, const uint64_t key, const CheckoutOwner role)
    -> ContentAcquisition
  {
    releases.RequireOpen();
    auto acquired = cache.Acquire<T>(key, role);
    if (!acquired) {
      return {};
    }
    ContentPublication publication { .value = acquired->value,
      .entry = acquired->ticket.Entry(),
      .type = T::ClassTypeId() };
    return { .publication = std::move(publication),
      .owner = releases.Own(cache, std::move(*acquired)) };
  }
};

//! Shared decode result. The temporary residency hold spans waiter delivery.
struct SharedContentResult final {
  ContentPublication publication {};
  std::shared_ptr<void> residency_hold {};

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return static_cast<bool>(publication.value);
  }

  template <IsTyped T>
  [[nodiscard]] static auto Publish(ContentReleaseQueue::Cache& cache,
    ContentReleaseQueue& releases, const uint64_t key, std::shared_ptr<T> value)
    -> SharedContentResult
  {
    releases.RequireOpen();
    if (auto acquired
      = cache.StoreAndAcquire(key, value, CheckoutOwner::kInternal,
        ContentReleaseQueue::Cache::UsageKind::kPin)) {
      ContentPublication publication { .value = std::move(value),
        .entry = acquired->ticket.Entry(),
        .type = T::ClassTypeId() };
      return { .publication = std::move(publication),
        .residency_hold = releases.Own(cache, std::move(*acquired)) };
    }
    return { .publication
      = { .value = std::move(value), .entry = {}, .type = T::ClassTypeId() },
      .residency_hold = {} };
  }

  template <IsTyped T>
  [[nodiscard]] static auto FromCache(ContentReleaseQueue::Cache& cache,
    ContentReleaseQueue& releases, const uint64_t key) -> SharedContentResult
  {
    releases.RequireOpen();
    auto acquired = cache.Acquire<T>(key, CheckoutOwner::kInternal,
      ContentReleaseQueue::Cache::UsageKind::kPin);
    if (!acquired) {
      return {};
    }
    ContentPublication publication { .value = acquired->value,
      .entry = acquired->ticket.Entry(),
      .type = T::ClassTypeId() };
    return { .publication = std::move(publication),
      .residency_hold = releases.Own(cache, std::move(*acquired)) };
  }
};

} // namespace oxygen::content::internal
