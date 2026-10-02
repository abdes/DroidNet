//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/Internal/ContentBindingBundle.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::internal {

auto ContentBindingBuilder::Freeze(ContentId identity,
  std::shared_ptr<const BindingViewId>
    view) && -> std::shared_ptr<const ContentBindingBundle>
{
  std::vector<BoundAsset> assets;
  std::vector<BoundResource> resources;
  assets.reserve(assets_.size());
  resources.reserve(resources_.size());
  for (auto& [key, binding] : assets_) {
    static_cast<void>(key);
    if (binding.owner) {
      assets.push_back(std::move(binding));
    }
  }
  for (auto& [key, binding] : resources_) {
    static_cast<void>(key);
    if (binding.owner) {
      resources.push_back(std::move(binding));
    }
  }
  return std::make_shared<const ContentBindingBundle>(identity, std::move(view),
    std::move(assets), std::move(resources), std::move(physics_));
}

ContentBindingBundle::ContentBindingBundle(ContentId identity,
  std::shared_ptr<const BindingViewId> view, std::vector<BoundAsset> assets,
  std::vector<BoundResource> resources,
  std::unique_ptr<const PhysicsBindings> physics)
  : identity_(identity)
  , view_(std::move(view))
  , assets_(std::move(assets))
  , resources_(std::move(resources))
  , physics_(std::move(physics))
{
  std::ranges::sort(assets_, {}, &BoundAsset::key);
  std::ranges::sort(resources_, {}, &BoundResource::key);
}

auto ContentBindingBundle::FindAssetBinding(
  const data::AssetKey& key) const noexcept -> const BoundAsset*
{
  const auto found
    = std::ranges::lower_bound(assets_, key, {}, &BoundAsset::key);
  if (found != assets_.end() && found->key == key) {
    return &*found;
  }
  for (const auto& asset : assets_) {
    const auto& retained = asset.owner->GetRuntimeBindings();
    if (retained && retained->GetTypeId() == ClassTypeId()) {
      const auto child
        = std::static_pointer_cast<const ContentBindingBundle>(retained);
      if (const auto* binding = child->FindAssetBinding(key)) {
        return binding;
      }
    }
  }
  return nullptr;
}

auto ContentBindingBundle::FindResourceBinding(ResourceKey key) const noexcept
  -> const BoundResource*
{
  const auto found
    = std::ranges::lower_bound(resources_, key, {}, &BoundResource::key);
  return found != resources_.end() && found->key == key ? &*found : nullptr;
}

auto ContentBindingBundle::FindAsset(const data::AssetKey& key) const noexcept
  -> std::shared_ptr<const data::Asset>
{
  const auto* binding = FindAssetBinding(key);
  return binding ? binding->owner : nullptr;
}

auto ContentBindingBundle::FindResource(const ResourceKey& key) const noexcept
  -> std::shared_ptr<const Object>
{
  const auto* binding = FindResourceBinding(key);
  return binding ? binding->owner : nullptr;
}

} // namespace oxygen::content::internal
