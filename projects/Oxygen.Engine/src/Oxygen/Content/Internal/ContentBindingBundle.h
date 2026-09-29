//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetRuntimeBindings.h>

namespace oxygen::content::internal {

struct BoundAsset final {
  data::AssetKey key {};
  ContentPublication publication {};
  std::shared_ptr<data::Asset> owner {};
};

struct BoundResource final {
  ResourceKey key {};
  ContentPublication publication {};
  std::shared_ptr<Object> owner {};
};

//! Frozen dependency controls and exact publication metadata, owned by Data.
class ContentBindingBundle final : public data::AssetRuntimeBindings {
  OXYGEN_TYPED(ContentBindingBundle)
public:
  ContentBindingBundle(
    std::vector<BoundAsset> assets, std::vector<BoundResource> resources);
  ~ContentBindingBundle() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ContentBindingBundle)
  OXYGEN_MAKE_NON_MOVABLE(ContentBindingBundle)

  [[nodiscard]] auto FindAsset(const data::AssetKey& key) const noexcept
    -> std::shared_ptr<const data::Asset> override;
  [[nodiscard]] auto FindResource(const ResourceKey& key) const noexcept
    -> std::shared_ptr<const Object> override;

  [[nodiscard]] auto FindAssetBinding(const data::AssetKey& key) const noexcept
    -> const BoundAsset*;
  [[nodiscard]] auto FindResourceBinding(ResourceKey key) const noexcept
    -> const BoundResource*;
  [[nodiscard]] auto Assets() const noexcept -> std::span<const BoundAsset>
  {
    return assets_;
  }
  [[nodiscard]] auto Resources() const noexcept
    -> std::span<const BoundResource>
  {
    return resources_;
  }

private:
  std::vector<BoundAsset> assets_;
  std::vector<BoundResource> resources_;
};

//! Collect once during binding, then compact into the immutable Data bundle.
class ContentBindingBuilder final {
public:
  explicit ContentBindingBuilder(std::shared_ptr<ContentReleaseQueue> epoch)
    : epoch_(std::move(epoch))
  {
  }
  ~ContentBindingBuilder() = default;
  OXYGEN_MAKE_NON_COPYABLE(ContentBindingBuilder)
  OXYGEN_DEFAULT_MOVABLE(ContentBindingBuilder)

  auto RequireOpen() const -> void { epoch_->RequireOpen(); }

  template <std::derived_from<data::Asset> T>
  [[nodiscard]] auto FindAsset(const data::AssetKey& key) const
    -> std::shared_ptr<T>
  {
    const auto found = assets_.find(key);
    return found != assets_.end()
        && found->second.publication.type == T::ClassTypeId()
      ? std::static_pointer_cast<T>(found->second.owner)
      : nullptr;
  }
  template <std::derived_from<Object> T>
  [[nodiscard]] auto FindResource(ResourceKey key) const -> std::shared_ptr<T>
  {
    const auto found = resources_.find(key);
    return found != resources_.end()
        && found->second.publication.type == T::ClassTypeId()
      ? std::static_pointer_cast<T>(found->second.owner)
      : nullptr;
  }
  [[nodiscard]] auto TryBeginAsset(const data::AssetKey& key, TypeId type)
    -> bool
  {
    return attempted_assets_.insert(AssetAttempt { .key = key, .type = type })
      .second;
  }
  [[nodiscard]] auto TryBeginResource(ResourceKey key) -> bool
  {
    return resources_
      .try_emplace(
        key, BoundResource { .key = key, .publication = {}, .owner = {} })
      .second;
  }

  template <std::derived_from<data::Asset> T>
  auto AddAsset(ContentAcquisition acquired) -> std::shared_ptr<T>
  {
    if (!acquired || acquired.publication.type != T::ClassTypeId()) {
      return {};
    }
    auto owned = std::static_pointer_cast<T>(std::move(acquired.owner));
    const auto [found, inserted]
      = assets_.insert_or_assign(owned->GetAssetKey(),
        BoundAsset { .key = owned->GetAssetKey(),
          .publication = std::move(acquired.publication),
          .owner = owned });
    static_cast<void>(inserted);
    return std::static_pointer_cast<T>(found->second.owner);
  }

  template <std::derived_from<Object> T>
  auto AddResource(ResourceKey key, ContentAcquisition acquired)
    -> std::shared_ptr<T>
  {
    if (!acquired || acquired.publication.type != T::ClassTypeId()) {
      return {};
    }
    auto owned = std::static_pointer_cast<T>(std::move(acquired.owner));
    const auto [found, inserted] = resources_.insert_or_assign(key,
      BoundResource { .key = key,
        .publication = std::move(acquired.publication),
        .owner = owned });
    static_cast<void>(inserted);
    return std::static_pointer_cast<T>(found->second.owner);
  }

  [[nodiscard]] auto Freeze() && -> std::shared_ptr<const ContentBindingBundle>;

private:
  struct AssetAttempt final {
    data::AssetKey key {};
    TypeId type { kInvalidTypeId };
    auto operator==(const AssetAttempt&) const -> bool = default;
  };
  struct AssetAttemptHash final {
    auto operator()(const AssetAttempt& attempt) const noexcept -> std::size_t
    {
      auto hash = std::hash<data::AssetKey> {}(attempt.key);
      HashCombine(hash, attempt.type);
      return hash;
    }
  };
  std::shared_ptr<ContentReleaseQueue> epoch_;
  std::unordered_set<AssetAttempt, AssetAttemptHash> attempted_assets_;
  std::unordered_map<data::AssetKey, BoundAsset> assets_;
  std::unordered_map<ResourceKey, BoundResource> resources_;
};

} // namespace oxygen::content::internal
