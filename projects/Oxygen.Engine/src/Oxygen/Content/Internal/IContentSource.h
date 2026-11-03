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
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>

namespace oxygen::content::internal {

using oxygen::base::IsAllZero;
using oxygen::base::Sha256Digest;

//! Minimal runtime-facing abstraction over a source of cooked bytes.
/*!
 A ContentSource provides cooked descriptor bytes and cooked resource bytes.
 It opens every device it reads when it is mounted: reading a descriptor, a
 resource table row or a payload opens nothing (a loose root's asset
 descriptors, one file each, are the exception).

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

  //! A reader of the descriptor of asset `key`, positioned at its start, with
  //! positions relative to it; null when the source does not hold it.
  /*!
   @throw data::CookedFormatError when the descriptor does not record its
     content hash.
  */
  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const -> std::unique_ptr<serio::AnyReader>;
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

  //! The table of the resource type `T`; null when the source holds none.
  template <PakResource T>
  [[nodiscard]] auto TableOf() const noexcept -> const ResourceTable<T>*
  {
    if constexpr (std::is_same_v<T, data::BufferResource>) {
      return GetBufferTable();
    } else if constexpr (std::is_same_v<T, data::TextureResource>) {
      return GetTextureTable();
    } else if constexpr (std::is_same_v<T, data::ScriptResource>) {
      return GetScriptTable();
    } else {
      static_assert(std::is_same_v<T, data::PhysicsResource>);
      return GetPhysicsTable();
    }
  }

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

  //! The descriptor bytes of resource `index` of `resource_type`, a view of
  //! the source's mapped table that is valid while the source lives; empty
  //! when the source holds no such resource.
  [[nodiscard]] auto ResourceDescriptorBytes(
    TypeId resource_type, data::pak::core::ResourceIndexT index) const
    -> std::optional<std::span<const std::byte>>;

  //! The region that holds the payloads of `resource_type`, on the device
  //! they are awaited from. A descriptor's `data_offset` is an offset of that
  //! device, so its payload starts `data_offset - Range().offset` into the
  //! region. Empty when the source holds no such payloads or reads none (an
  //! index-only source).
  [[nodiscard]] virtual auto PayloadRegion(TypeId resource_type) const noexcept
    -> std::optional<serio::Region<serio::AnyAsyncDevice>> = 0;

  //! The payload of `resource_type` at `extent`, a range in the space its
  //! descriptors' `data_offset` names; empty when the source holds no such
  //! payloads or `extent` lies outside them.
  [[nodiscard]] auto PayloadOf(
    TypeId resource_type, serio::ByteRange extent) const
    -> std::optional<serio::Region<serio::AnyAsyncDevice>>;

  //! The part of `payloads` at `extent`, a range in the space
  //! `payloads.Range()` is in; empty when `extent` lies outside `payloads`.
  [[nodiscard]] static auto PayloadWithin(
    const serio::Region<serio::AnyAsyncDevice>& payloads,
    serio::ByteRange extent)
    -> std::optional<serio::Region<serio::AnyAsyncDevice>>;

  [[nodiscard]] virtual auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept -> std::optional<std::string> = 0;

  //! Texture table index of the texture descriptor recorded under
  //! `virtual_path`, or nullopt when this source records none.
  /*!
   @throw data::CookedFormatError when the recorded descriptor is not one, has
     another version, is malformed, or disagrees with its texture table.
   @throw std::system_error when it cannot be read.
  */
  [[nodiscard]] virtual auto FindTextureIndexByVirtualPath(
    std::string_view virtual_path) const
    -> std::optional<data::pak::core::ResourceIndexT> = 0;

  //! Texture table index of the texture whose key
  //! (`data::TextureKeyFromVirtualPath`) is `key`, or nullopt when this source
  //! holds no such texture.
  [[nodiscard]] auto FindTextureIndexByKey(const data::AssetKey& key) const
    -> std::optional<data::pak::core::ResourceIndexT>;

  //! Virtual path of the texture whose key is `key`, or nullopt when this
  //! source holds no such texture.
  [[nodiscard]] auto FindTextureVirtualPath(const data::AssetKey& key) const
    -> std::optional<std::string>;

  //! Virtual paths of the texture descriptors this source records.
  [[nodiscard]] virtual auto TextureVirtualPaths() const
    -> std::vector<std::string> = 0;

private:
  mutable std::once_flag texture_key_once_;
  mutable std::unordered_map<data::AssetKey, std::string> texture_paths_by_key_;
  mutable std::once_flag physics_index_once_;
  mutable std::unordered_map<data::AssetKey, data::pak::core::ResourceIndexT>
    physics_index_;
  [[nodiscard]] virtual auto ReadAssetReferenceMetadata(
    const data::AssetKey& key) const -> data::AssetReferences = 0;

  //! A reader of the descriptor of asset `key`, as the source holds it,
  //! positioned at its start; null when the source does not hold it.
  [[nodiscard]] virtual auto OpenAssetDescriptor(
    const data::AssetKey& key) const -> std::unique_ptr<serio::AnyReader> = 0;

  //! The row space of `resource_type`'s table: the bytes its row offsets are
  //! offsets of. A pak's offsets are offsets of the pak, so its row space is
  //! the whole pak; a loose root's are offsets of the table file, so its row
  //! space is that table. Empty when the source holds no such table.
  [[nodiscard]] virtual auto ResourceRowSpace(
    TypeId resource_type) const noexcept -> std::span<const std::byte> = 0;
};

} // namespace oxygen::content::internal
