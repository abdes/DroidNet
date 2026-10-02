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
#include <Oxygen/Data/AssetKey.h>
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

  data::SourceInstanceId id {};
  std::shared_ptr<const IContentSource> source {};
  std::unordered_set<data::AssetKey> deleted {};
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
  [[nodiscard]] auto ResolveAsset(const data::AssetKey& key) const noexcept
    -> std::optional<data::SourceInstanceId>
  {
    for (auto layer = layers_.rbegin(); layer != layers_.rend(); ++layer) {
      if (layer->deleted.contains(key)) {
        return std::nullopt;
      }
      if (layer->source->HasAsset(key)) {
        return layer->id;
      }
    }
    return std::nullopt;
  }

private:
  std::shared_ptr<const BindingViewId> identity_;
  std::vector<ContentSourceLayer> layers_;
};

} // namespace oxygen::content::internal
