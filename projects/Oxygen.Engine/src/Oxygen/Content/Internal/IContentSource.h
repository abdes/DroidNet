//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {

using oxygen::base::ComputeFileSha256;
using oxygen::base::IsAllZero;
using oxygen::base::Sha256Digest;

//! Minimal runtime-facing abstraction over a source of cooked bytes.
/*!
 A ContentSource provides cooked descriptor bytes and cooked resource bytes.

 This is an internal runtime abstraction used by the loader pipeline to treat
 different storage forms uniformly (e.g. `.pak` vs loose cooked directories).

 It is not an editor mount-point abstraction.
*/
class IContentSource : public oxygen::Object {
public:
  ~IContentSource() override = default;

  IContentSource() = default;

  IContentSource(const IContentSource&) = delete;
  IContentSource(IContentSource&&) = delete;
  auto operator=(const IContentSource&) -> IContentSource& = delete;
  auto operator=(IContentSource&&) -> IContentSource& = delete;

  [[nodiscard]] virtual auto DebugName() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto SourcePath() const noexcept
    -> const std::filesystem::path& = 0;

  [[nodiscard]] virtual auto GetSourceKey() const noexcept -> data::SourceKey
    = 0;

  [[nodiscard]] virtual auto GetPakCatalog() const noexcept
    -> const data::PakCatalog*
  {
    return nullptr;
  }

  [[nodiscard]] virtual auto GetAssetType(
    const data::AssetKey& key) const noexcept
    -> std::optional<data::AssetType> = 0;

  [[nodiscard]] virtual auto HasAsset(const data::AssetKey& key) const noexcept
    -> bool = 0;
  [[nodiscard]] virtual auto HasKeyReferences(
    const data::AssetKey& key) const noexcept -> bool = 0;
  [[nodiscard]] virtual auto FindAssetKeyByVirtualPath(
    std::string_view path) const -> std::optional<data::AssetKey> = 0;
  [[nodiscard]] virtual auto GetAssetCount() const noexcept -> size_t = 0;
  [[nodiscard]] virtual auto GetAssetKeyByIndex(uint32_t index) const noexcept
    -> std::optional<data::AssetKey> = 0;

  [[nodiscard]] virtual auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const -> std::unique_ptr<serio::AnyReader> = 0;
  [[nodiscard]] auto ReadAssetReferences(const data::AssetKey& key) const
    -> data::AssetReferences
  {
    auto references = ReadAssetReferenceMetadata(key);
    const auto count = [](const auto* table) -> uint64_t {
      return table ? table->Size().get() : 0U;
    };
    const auto valid = references.ValidateResourceBounds({
      .buffers = count(GetBufferTable()),
      .textures = count(GetTextureTable()),
      .scripts = count(GetScriptTable()),
      .physics = count(GetPhysicsTable()),
    });
    if (!valid) {
      throw std::runtime_error(valid.error());
    }
    return references;
  }

  [[nodiscard]] virtual auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* = 0;

  [[nodiscard]] virtual auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* = 0;

  [[nodiscard]] virtual auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* = 0;

  [[nodiscard]] auto FindPhysicsResource(const data::AssetKey& key) const
    -> std::optional<data::pak::core::ResourceIndexT>;

  [[nodiscard]] virtual auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* = 0;

  [[nodiscard]] virtual auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader> = 0;

  [[nodiscard]] virtual auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept -> std::optional<std::string> = 0;

private:
  mutable std::once_flag physics_index_once_ {};
  mutable std::unordered_map<data::AssetKey, data::pak::core::ResourceIndexT>
    physics_index_ {};
  [[nodiscard]] virtual auto ReadAssetReferenceMetadata(
    const data::AssetKey& key) const -> data::AssetReferences = 0;
};

} // namespace oxygen::content::internal
