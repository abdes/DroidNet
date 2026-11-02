//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

struct ContentSourceLayer final {
  ContentSourceLayer(data::SourceInstanceId source_id,
    std::shared_ptr<const IContentSource> owner)
    : id(source_id)
    , source(std::move(owner))
  {
    if (const auto* catalog = source->GetPakCatalog()) {
      deleted.insert(catalog->deleted.begin(), catalog->deleted.end());
    }
  }

  data::SourceInstanceId id;
  std::shared_ptr<const IContentSource> source;
  std::unordered_set<data::AssetKey> deleted;
};

//! Immutable lookup metadata retained only while a load scope is active.
class ContentSourceView final {
public:
  ContentSourceView(
    BindingViewId identity, std::vector<ContentSourceLayer> layers)
    : identity_(std::make_shared<const BindingViewId>(identity))
    , layers_(std::move(layers))
  {
  }
  ~ContentSourceView() = default;
  OXYGEN_MAKE_NON_COPYABLE(ContentSourceView)
  OXYGEN_MAKE_NON_MOVABLE(ContentSourceView)

  [[nodiscard]] auto Identity() const noexcept -> BindingViewId
  {
    return *identity_;
  }
  [[nodiscard]] auto IdentityOwner() const noexcept
    -> const std::shared_ptr<const BindingViewId>&
  {
    return identity_;
  }
  [[nodiscard]] auto Layers() const noexcept
    -> std::span<const ContentSourceLayer>
  {
    return layers_;
  }
  //! The layer whose asset `key` wins, unless a newer layer deletes it.
  [[nodiscard]] auto ResolveAsset(const data::AssetKey& key) const
    -> std::optional<data::SourceInstanceId>
  {
    const auto match = ResolveByPrecedence(
      Layers(),
      [&](const ContentSourceLayer& layer) -> bool {
        return layer.deleted.contains(key);
      },
      [&](const ContentSourceLayer& layer) -> bool {
        return layer.source->HasAsset(key);
      });
    return match.status == KeyResolutionStatus::kFound
      ? std::optional { match.layer->id }
      : std::nullopt;
  }

  //! A texture of a layer's texture table, selected by its key.
  struct TextureMatch final {
    const ContentSourceLayer* layer { nullptr };
    data::pak::core::ResourceIndexT index;
  };

  //! The layer whose texture `key` wins, unless a newer layer deletes it.
  [[nodiscard]] auto ResolveTexture(const data::AssetKey& key) const
    -> std::optional<TextureMatch>
  {
    auto index = std::optional<data::pak::core::ResourceIndexT> {};
    const auto match = ResolveByPrecedence(
      Layers(),
      [&](const ContentSourceLayer& layer) -> bool {
        return layer.deleted.contains(key);
      },
      [&](const ContentSourceLayer& layer) -> bool {
        index = layer.source->FindTextureIndexByKey(key);
        return index.has_value();
      });
    if (match.status != KeyResolutionStatus::kFound || !index.has_value()) {
      return std::nullopt;
    }
    return TextureMatch { .layer = match.layer, .index = *index };
  }

private:
  std::shared_ptr<const BindingViewId> identity_;
  std::vector<ContentSourceLayer> layers_;
};

} // namespace oxygen::content::internal
