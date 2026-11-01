//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

//! A mounted pak: its mapped metadata, and the file its payloads are awaited
//! from, opened through the loader's driver.
/*!
 `PakFile` parses the mapping; the source owns the payload file, because only
 the loader awaits payloads. A verified mount checks the CRC32 before the
 source is made (`VerifyPakAsync`), so the source itself verifies only when
 asked to by a synchronous mount.
*/
class PakFileSource final : public IContentSource {
public:
  OXYGEN_TYPED(PakFileSource)

public:
  //! Mounts the pak at `pak_path`; with `driver`, its payloads are awaited
  //! through it.
  PakFileSource(const std::filesystem::path& pak_path,
    const bool verify_content_hashes,
    const std::shared_ptr<serio::IoDriver>& driver)
    // `driver` is copied: a moved argument could be emptied before
    // `OpenPayloads` reads it, as argument order is unspecified.
    : PakFileSource(pak_path, VerificationOf(verify_content_hashes),
        OpenPayloads(pak_path, *driver), driver)
  {
  }

  //! Mounts the pak at `pak_path`, whose payload file `payloads` was opened
  //! through `driver` (and verified with it) before.
  PakFileSource(const std::filesystem::path& pak_path,
    serio::AsyncFile payloads, std::shared_ptr<serio::IoDriver> driver)
    : PakFileSource(pak_path, PakFile::Verification::kNone, std::move(payloads),
        std::move(driver))
  {
  }

  ~PakFileSource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PakFileSource)
  OXYGEN_MAKE_NON_MOVABLE(PakFileSource)

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }
  [[nodiscard]] auto SourcePath() const noexcept
    -> const std::filesystem::path& override
  {
    return pak_.FilePath();
  }

  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return pak_.Guid();
  }

  [[nodiscard]] auto HasAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return pak_.FindEntry(key).has_value();
  }

  [[nodiscard]] auto GetPakCatalog() const noexcept
    -> const data::PakCatalog* override
  {
    return &pak_.Catalog();
  }

  [[nodiscard]] auto HasKeyReferences(const data::AssetKey& key) const noexcept
    -> bool override
  {
    const auto entry = pak_.FindEntry(key);
    return entry && entry->references.key_count != 0U;
  }

  [[nodiscard]] auto FindAssetKeyByVirtualPath(
    const std::string_view path) const -> std::optional<data::AssetKey> override
  {
    return pak_.ResolveAssetKeyByVirtualPath(path);
  }

  [[nodiscard]] auto GetAssetType(const data::AssetKey& key) const noexcept
    -> std::optional<data::AssetType> override
  {
    const auto entry = pak_.FindEntry(key);
    return entry ? std::optional { entry->asset_type } : std::nullopt;
  }

  [[nodiscard]] auto GetAssetCount() const noexcept -> size_t override
  {
    return pak_.Directory().size();
  }

  [[nodiscard]] auto GetAssetKeyByIndex(const uint32_t index) const noexcept
    -> std::optional<data::AssetKey> override
  {
    const auto directory = pak_.Directory();
    if (index >= directory.size()) {
      return std::nullopt;
    }
    return std::ranges::next(directory.begin(), index)->asset_key;
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto entry = pak_.FindEntry(key);
    if (!entry.has_value()) {
      return nullptr;
    }
    auto region = serio::AnyRegion::Make(pak_.Device(),
      {
        .offset = serio::ByteOffset { entry->desc_offset },
        .count = serio::ByteCount { entry->desc_size },
      });
    if (!region) {
      throw std::out_of_range("PAK asset descriptor is out of bounds");
    }
    return std::make_unique<serio::RegionReader<serio::AnyDevice>>(*region);
  }

  [[nodiscard]] auto ReadAssetReferenceMetadata(const data::AssetKey& key) const
    -> data::AssetReferences override
  {
    return pak_.ReadAssetReferences(key);
  }

  [[nodiscard]] auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* override
  {
    return pak_.GetResourceTable<data::BufferResource>();
  }

  [[nodiscard]] auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* override
  {
    return pak_.GetResourceTable<data::TextureResource>();
  }

  [[nodiscard]] auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* override
  {
    return pak_.GetResourceTable<data::ScriptResource>();
  }

  [[nodiscard]] auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* override
  {
    return pak_.GetResourceTable<data::PhysicsResource>();
  }

  [[nodiscard]] auto PayloadRegion(const TypeId resource_type) const noexcept
    -> std::optional<serio::Region<serio::AnyAsyncDevice>> override
  {
    return VisitResourceType(
      resource_type,
      [this]<typename T> -> PayloadRegionOpt {
        return payloads_.at(kResourceTypeIndex<T>);
      },
      [] -> PayloadRegionOpt { return std::nullopt; });
  }

  [[nodiscard]] auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string> override
  {
    for (const auto& entry : pak_.BrowseIndex()) {
      if (entry.asset_key == key) {
        return entry.virtual_path;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto FindTextureIndexByVirtualPath(
    const std::string_view virtual_path) const
    -> std::optional<data::pak::core::ResourceIndexT> override
  {
    return pak_.ResolveTextureIndexByVirtualPath(virtual_path);
  }

  [[nodiscard]] auto TextureVirtualPaths() const
    -> std::vector<std::string> override
  {
    std::vector<std::string> paths;
    paths.reserve(pak_.BrowseTextures().size());
    for (const auto& texture : pak_.BrowseTextures()) {
      paths.push_back(texture.virtual_path);
    }
    return paths;
  }

  [[nodiscard]] auto Pak() const noexcept -> const PakFile& { return pak_; }

  //! The device the pak's payloads are awaited from.
  [[nodiscard]] auto Payloads() const noexcept -> const serio::AnyAsyncDevice&
  {
    return payload_file_;
  }

private:
  using PayloadRegionOpt = std::optional<serio::Region<serio::AnyAsyncDevice>>;

  [[nodiscard]] auto ResourceRowSpace(const TypeId resource_type) const noexcept
    -> std::span<const std::byte> override
  {
    using Space = std::span<const std::byte>;
    // Pak table offsets are offsets of the pak.
    return VisitResourceType(
      resource_type,
      [this]<typename T> -> Space {
        return pak_.HasTableOf<T>() ? pak_.Bytes() : Space {};
      },
      [] -> Space { return {}; });
  }

  PakFileSource(const std::filesystem::path& pak_path,
    const PakFile::Verification verification, serio::AsyncFile payloads,
    std::shared_ptr<serio::IoDriver> driver)
    : driver_(std::move(driver))
    , pak_(pak_path, verification)
    , payload_file_(std::move(payloads))
    , debug_name_(pak_.FilePath().string())
  {
    Mount();
  }

  //! Opens the pak file through `driver` for its payloads.
  static auto OpenPayloads(const std::filesystem::path& pak_path,
    serio::IoDriver& driver) -> serio::AsyncFile
  {
    auto file = driver.Open(pak_path, {});
    if (!file) {
      LOG_F(ERROR, "Failed to open pak file '{}' for payloads: {}",
        pak_path.string(), file.error().message());
      throw std::system_error(file.error(), "open '" + pak_path.string() + "'");
    }
    return std::move(file).value();
  }

  //! A synchronous verified mount checks the pak's CRC32 as it maps it.
  static auto VerificationOf(const bool verify_content_hashes) noexcept
    -> PakFile::Verification
  {
    return verify_content_hashes ? PakFile::Verification::kCrc32
                                 : PakFile::Verification::kNone;
  }

  auto Mount() -> void
  {
    const auto& footer = pak_.Footer();
    PayloadsOf<data::TextureResource>(footer.texture_region);
    PayloadsOf<data::BufferResource>(footer.buffer_region);
    PayloadsOf<data::ScriptResource>(footer.script_region);
    PayloadsOf<data::PhysicsResource>(footer.physics_region);
  }

  //! Records the payload region of `T`, when the pak holds a table of them.
  template <PakResource T>
  auto PayloadsOf(const data::pak::core::ResourceRegion& region) -> void
  {
    if (!pak_.HasTableOf<T>()) {
      return;
    }
    auto payloads = serio::Region<serio::AnyAsyncDevice>::Make(payload_file_,
      {
        .offset = serio::ByteOffset { region.offset },
        .count = serio::ByteCount { region.size },
      });
    if (!payloads) {
      throw std::runtime_error("PAK payload region is out of bounds");
    }
    payloads_.at(kResourceTypeIndex<T>) = *payloads;
  }

  //! Declared first: the driver outlives the payload file it opened.
  std::shared_ptr<serio::IoDriver> driver_;
  PakFile pak_;
  //! Declared before the regions that view it.
  serio::AnyAsyncDevice payload_file_;
  std::string debug_name_;
  std::array<PayloadRegionOpt, kResourceTypeCount> payloads_ {};
};

} // namespace oxygen::content::internal
