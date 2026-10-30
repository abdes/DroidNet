//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::testing {

//! Forges a minimal pak for `PakFile` and `AssetLoader` tests.
/*!
 Header-only. Writes a header, optional buffer table, optional physics region
 and table, the listed assets with zero-filled descriptors, the browse index,
 an embedded catalog, and a footer, in the same shape the production PakWriter
 emits. The catalog digest is computed the way `PakFile` expects.
*/
class PakTestWriter final {
public:
  struct Config {
    data::pak::core::PakHeader header {};
    data::pak::core::PakFooter footer {};
    std::vector<data::pak::core::BufferResourceDesc> buffers;

    //! One physics payload and its descriptor (data_offset/size_bytes are
    //! filled by the writer; the caller sets format/asset key/content hash).
    struct PhysicsEntry {
      data::pak::physics::PhysicsResourceDesc desc {};
      std::vector<std::byte> payload;
    };
    std::vector<PhysicsEntry> physics_resources;

    //! Asset keys copied into `catalog.deleted`.
    std::vector<data::AssetKey> deleted;

    //! An asset the pak lists in its directory, browse index and catalog.
    //! Its descriptor is zero-filled, so it is never loaded.
    struct Asset {
      data::AssetKey key;
      data::AssetType type = data::AssetType::kMaterial;
      std::string virtual_path;
    };
    std::vector<Asset> assets;

    //! The catalog's library, the root of every asset's virtual path.
    std::string library = "Tests";

    //! Copied into `catalog.dependencies`.
    std::vector<data::PakCatalogDependency> dependencies;

    //! Source identity of both the header and the catalog. Without it the
    //! catalog uses the default identity, whatever `header` holds.
    std::optional<data::SourceKey> source_key;

    Config();
  };

  explicit PakTestWriter(std::filesystem::path pak_path);

  //! Writes the pak to `pak_path_`.
  void Write(const Config& config);

  //! The bytes of the pak `config` describes.
  [[nodiscard]] static auto Encode(const Config& config)
    -> std::vector<std::byte>;

private:
  std::filesystem::path pak_path_;
};

inline PakTestWriter::Config::Config()
{
  const std::span<const char> header_magic(data::pak::core::kPakHeaderMagic);
  std::ranges::copy(header_magic, std::ranges::begin(header.magic));
  header.version = data::pak::core::kCurrentPakFormatVersion;
  constexpr std::array<uint8_t, 16> kSourceIdentity {
    0x41U,
    0x42U,
    0x43U,
    0x44U,
    0x45U,
    0x46U,
    0x77U,
    0x48U,
    0x89U,
    0x4AU,
    0x4BU,
    0x4CU,
    0x4DU,
    0x4EU,
    0x4FU,
    0x50U,
  };
  header.source_identity = kSourceIdentity;

  const std::span<const char> footer_magic(data::pak::core::kPakFooterMagic);
  std::ranges::copy(footer_magic, std::ranges::begin(footer.footer_magic));
}

inline PakTestWriter::PakTestWriter(std::filesystem::path pak_path)
  : pak_path_(std::move(pak_path))
{
}

inline void PakTestWriter::Write(const Config& config)
{
  std::ofstream out(pak_path_, std::ios::binary);
  std::ranges::transform(Encode(config), std::ostreambuf_iterator<char>(out),
    [](const std::byte b) -> char { return static_cast<char>(b); });
  ASSERT_TRUE(out.good());
}

inline auto PakTestWriter::Encode(const Config& config)
  -> std::vector<std::byte>
{
  // The catalog carries the fixture's default identity unless `source_key`
  // overrides both it and the header, so a test can make the two disagree by
  // editing only `header.source_identity`.
  auto header = config.header;
  auto catalog_key
    = data::SourceKey::FromBytes(Config {}.header.source_identity).value();
  if (config.source_key.has_value()) {
    catalog_key = *config.source_key;
    const auto key_bytes = data::as_bytes(catalog_key);
    std::memcpy(
      header.source_identity.data(), key_bytes.data(), key_bytes.size());
  }

  auto catalog = data::PakCatalog {
    .source_key = catalog_key,
    .content_version = header.content_version,
    .catalog_digest = {},
    .library = config.library,
    .entries = {},
    .deleted = config.deleted,
    .bases = {},
    .dependencies = config.dependencies,
  };
  for (const auto& asset : config.assets) {
    catalog.entries.push_back({
      .asset_key = asset.key,
      .asset_type = asset.type,
      .descriptor_digest = {},
      .transitive_resource_digest = {},
    });
  }
  catalog.catalog_digest = catalog.ComputeDigest().value();
  const auto catalog_bytes = catalog.Encode().value();

  auto bytes = std::vector<std::byte> {};
  const auto append = [&bytes](const std::span<const std::byte> blob) {
    bytes.insert(bytes.end(), blob.begin(), blob.end());
  };
  append(std::as_bytes(std::span { &header, 1U }));

  auto footer = config.footer;
  auto offset = uint64_t { sizeof(header) };

  if (!config.buffers.empty()) {
    footer.buffer_table = {
      .offset = offset,
      .count = static_cast<uint32_t>(config.buffers.size()),
      .entry_size = sizeof(data::pak::core::BufferResourceDesc),
    };
    const auto buffer_bytes = std::as_bytes(std::span(config.buffers));
    append(buffer_bytes);
    offset += buffer_bytes.size();
  }

  if (!config.physics_resources.empty()) {
    // Payload region first.
    footer.physics_region.offset = offset;
    uint64_t region_size = 0;
    for (const auto& entry : config.physics_resources) {
      region_size += entry.payload.size();
    }
    footer.physics_region.size = region_size;

    std::vector<data::pak::physics::PhysicsResourceDesc> descriptors;
    descriptors.reserve(config.physics_resources.size());
    uint64_t data_offset = footer.physics_region.offset;
    for (const auto& entry : config.physics_resources) {
      auto desc = entry.desc;
      desc.data_offset = data_offset;
      desc.size_bytes
        = static_cast<data::pak::core::DataBlobSizeT>(entry.payload.size());
      descriptors.push_back(desc);
      data_offset += entry.payload.size();
    }

    for (const auto& entry : config.physics_resources) {
      if (!entry.payload.empty()) {
        append(std::as_bytes(std::span(entry.payload)));
      }
    }
    offset += region_size;

    footer.physics_resource_table = {
      .offset = offset,
      .count = static_cast<uint32_t>(descriptors.size()),
      .entry_size = sizeof(data::pak::physics::PhysicsResourceDesc),
    };
    const auto table_bytes = std::as_bytes(std::span(descriptors));
    append(table_bytes);
    offset += table_bytes.size();
  }

  constexpr auto kDescriptorSize = uint32_t { 64U };
  auto directory = std::vector<data::pak::core::AssetDirectoryEntry> {};
  for (const auto& asset : config.assets) {
    directory.push_back({
      .asset_key = asset.key,
      .asset_type = asset.type,
      .entry_offset = 0U,
      .desc_offset = offset,
      .desc_size = kDescriptorSize,
      .references = {},
    });
    append(std::vector<std::byte>(kDescriptorSize));
    offset += kDescriptorSize;
  }
  footer.directory_offset = offset;
  footer.directory_size = directory.size() * sizeof(directory.front());
  footer.asset_count = directory.size();
  for (auto& entry : directory) {
    entry.entry_offset = offset;
    offset += sizeof(entry);
  }
  append(std::as_bytes(std::span(directory)));

  // The browse index: its header, one entry per asset, then their paths.
  auto strings = std::string {};
  auto browse = std::vector<data::pak::core::PakBrowseIndexEntry> {};
  for (const auto& asset : config.assets) {
    browse.push_back({
      .asset_key = asset.key,
      .virtual_path_offset
      = static_cast<data::pak::core::StringTableOffsetT>(strings.size()),
      .virtual_path_length = static_cast<data::pak::core::StringTableSizeT>(
        asset.virtual_path.size()),
    });
    strings += asset.virtual_path;
  }
  auto browse_header = data::pak::core::PakBrowseIndexHeader {};
  browse_header.entry_count = static_cast<uint32_t>(browse.size());
  browse_header.string_table_size
    = static_cast<data::pak::core::StringTableSizeT>(strings.size());
  footer.browse_index_offset = offset;
  append(std::as_bytes(std::span { &browse_header, 1U }));
  append(std::as_bytes(std::span(browse)));
  append(std::as_bytes(std::span(strings)));
  footer.browse_index_size = sizeof(browse_header)
    + (browse.size() * sizeof(data::pak::core::PakBrowseIndexEntry))
    + strings.size();
  offset += footer.browse_index_size;

  append(std::as_bytes(std::span(catalog_bytes)));
  footer.catalog_offset = offset;
  footer.catalog_size = catalog_bytes.size();
  append(std::as_bytes(std::span { &footer, 1U }));
  return bytes;
}

} // namespace oxygen::content::testing
