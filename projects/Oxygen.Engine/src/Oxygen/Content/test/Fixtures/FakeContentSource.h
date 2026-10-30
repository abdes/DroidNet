//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::testing {

//! A content layer held in memory, for the registry's mount rules.
/*!
 It lists assets by key, with their type and virtual path, and textures by
 virtual path; it holds no bytes, so nothing in it loads. A layer with a
 catalog acts as a pak: the catalog's deletions tombstone lower assets and its
 dependencies are checked when it mounts.
*/
class FakeContentSource final : public internal::IContentSource {
public:
  OXYGEN_TYPED(FakeContentSource)

public:
  explicit FakeContentSource(
    std::string name, const data::SourceKey source_key = {})
    : name_(std::move(name))
    , path_(name_)
    , source_key_(source_key)
  {
  }

  //! Lists the asset at `virtual_path`, keyed from it.
  auto AddAsset(std::string virtual_path, const data::AssetType type)
    -> data::AssetKey
  {
    const auto key = data::AssetKey::FromVirtualPath(virtual_path);
    assets_.insert_or_assign(
      key, Asset { .type = type, .virtual_path = std::move(virtual_path) });
    return key;
  }

  //! Lists the texture descriptor at `virtual_path`, before the layer is
  //! first asked for a texture: the layer reads its texture paths once.
  auto AddTexture(std::string virtual_path) -> void
  {
    textures_.push_back(std::move(virtual_path));
  }

  //! Makes this layer a pak with `catalog`.
  auto SetCatalog(data::PakCatalog catalog) -> void
  {
    catalog_ = std::move(catalog);
  }

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return name_;
  }
  [[nodiscard]] auto SourcePath() const noexcept
    -> const std::filesystem::path& override
  {
    return path_;
  }
  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return source_key_;
  }
  [[nodiscard]] auto GetPakCatalog() const noexcept
    -> const data::PakCatalog* override
  {
    return catalog_ ? &*catalog_ : nullptr;
  }
  [[nodiscard]] auto GetAssetType(const data::AssetKey& key) const noexcept
    -> std::optional<data::AssetType> override
  {
    const auto found = assets_.find(key);
    return found == assets_.end() ? std::nullopt
                                  : std::optional(found->second.type);
  }
  [[nodiscard]] auto HasAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return assets_.contains(key);
  }
  [[nodiscard]] auto HasKeyReferences(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }
  [[nodiscard]] auto FindAssetKeyByVirtualPath(
    const std::string_view path) const -> std::optional<data::AssetKey> override
  {
    for (const auto& [key, asset] : assets_) {
      if (asset.virtual_path == path) {
        return key;
      }
    }
    return std::nullopt;
  }
  [[nodiscard]] auto GetAssetCount() const noexcept -> size_t override
  {
    return assets_.size();
  }
  [[nodiscard]] auto GetAssetKeyByIndex(const uint32_t index) const noexcept
    -> std::optional<data::AssetKey> override
  {
    if (index >= assets_.size()) {
      return std::nullopt;
    }
    return std::ranges::next(assets_.begin(), index)->first;
  }
  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& /*key*/) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* override
  {
    return nullptr;
  }
  [[nodiscard]] auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* override
  {
    return nullptr;
  }
  [[nodiscard]] auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* override
  {
    return nullptr;
  }
  [[nodiscard]] auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }
  [[nodiscard]] auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string> override
  {
    const auto found = assets_.find(key);
    return found == assets_.end() ? std::nullopt
                                  : std::optional(found->second.virtual_path);
  }
  [[nodiscard]] auto FindTextureIndexByVirtualPath(
    const std::string_view /*virtual_path*/) const
    -> std::optional<data::pak::core::ResourceIndexT> override
  {
    return std::nullopt;
  }
  [[nodiscard]] auto TextureVirtualPaths() const
    -> std::vector<std::string> override
  {
    return textures_;
  }

private:
  struct Asset {
    data::AssetType type = data::AssetType::kUnknown;
    std::string virtual_path;
  };

  [[nodiscard]] auto ReadAssetReferenceMetadata(
    const data::AssetKey& /*key*/) const -> data::AssetReferences override
  {
    return {};
  }

  std::string name_;
  std::filesystem::path path_;
  data::SourceKey source_key_;
  std::map<data::AssetKey, Asset> assets_;
  std::vector<std::string> textures_;
  std::optional<data::PakCatalog> catalog_;
};

} // namespace oxygen::content::testing
