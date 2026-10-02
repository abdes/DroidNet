//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>

namespace oxygen::content::internal {

//! Loader-thread identity storage. Decoded-cache eviction does not erase
//! locators.
class ContentIdentityRegistry final {
public:
  using HashFunction = auto (*)(const ContentIdentity&) noexcept -> size_t;

  explicit ContentIdentityRegistry(
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource(),
    HashFunction hash = HashIdentity);
  ~ContentIdentityRegistry() = default;
  OXYGEN_MAKE_NON_COPYABLE(ContentIdentityRegistry)
  OXYGEN_MAKE_NON_MOVABLE(ContentIdentityRegistry)

  [[nodiscard]] auto Intern(const ContentIdentity& identity,
    const std::shared_ptr<const BindingViewId>& view = {}) -> ContentId;
  [[nodiscard]] auto Find(const ContentIdentity& identity) const noexcept
    -> ContentId;
  [[nodiscard]] auto Resolve(ContentId id) const noexcept
    -> const ContentIdentity*;
  [[nodiscard]] auto FindAsset(ContentId id) const noexcept
    -> const AssetIdentity*;
  [[nodiscard]] auto FindCookedResource(ContentId id) const noexcept
    -> const CookedResourceIdentity*;
  [[nodiscard]] auto FindResourceKind(ContentId id) const noexcept
    -> std::optional<ResourceKind>;

  //! Forget locators only after their source or synthetic producer is
  //! unreadable.
  auto Erase(ContentId id) -> void;
  auto EraseSources(const std::unordered_set<data::SourceInstanceId>& sources)
    -> void;
  auto Clear() noexcept -> void;
  //! Reclaim expired binding-view locators with bounded owner-thread work.
  auto ProcessExpiredViews(size_t max_work) -> size_t;

  [[nodiscard]] auto Size() const noexcept -> size_t;
  [[nodiscard]] auto Entries() const noexcept
    -> const std::pmr::unordered_map<ContentId, const ContentIdentity*>&;

private:
  static auto HashIdentity(const ContentIdentity& identity) noexcept -> size_t;
  auto RegisterViewIdentity(
    ContentId id, const std::shared_ptr<const BindingViewId>& view) -> void;

  struct ViewBucket {
    std::weak_ptr<const BindingViewId> lifetime {};
    std::pmr::vector<ContentId> identities {};
  };

  std::pmr::unordered_map<ContentIdentity, ContentId, HashFunction> identities_;
  std::pmr::unordered_map<ContentId, const ContentIdentity*> identities_by_id_;
  std::pmr::unordered_map<BindingViewId, ViewBucket> view_buckets_;
  std::pmr::vector<BindingViewId> view_order_;
  size_t view_cursor_ = 0;
  uint64_t next_id_ = 1;
};

} // namespace oxygen::content::internal
