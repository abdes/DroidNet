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
#include <ios>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::testing {

//! Forges a minimal pak for `PakFile` and `AssetLoader` tests.
/*!
 Header-only. Writes a header, optional buffer table, optional physics region
 and table, an embedded catalog, and a footer, in the same shape the production
 PakWriter emits. The catalog digest is computed the way `PakFile` expects.
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

    //! Source identity of both the header and the catalog. Without it the
    //! catalog uses the default identity, whatever `header` holds.
    std::optional<data::SourceKey> source_key;

    Config();
  };

  explicit PakTestWriter(std::filesystem::path pak_path);

  //! Writes the pak to `pak_path_`.
  void Write(const Config& config);

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
    .entries = {},
    .deleted = config.deleted,
    .bases = {},
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  const auto catalog_bytes = catalog.Encode().value();

  serio::FileStream<> stream(
    pak_path_, std::ios::out | std::ios::trunc | std::ios::binary);
  serio::Writer writer(stream);
  const auto packed = writer.ScopedAlignment(1U);
  ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span { &header, 1U })));

  auto footer = config.footer;
  auto offset = uint64_t { sizeof(header) };

  if (!config.buffers.empty()) {
    footer.buffer_table = {
      .offset = offset,
      .count = static_cast<uint32_t>(config.buffers.size()),
      .entry_size = sizeof(data::pak::core::BufferResourceDesc),
    };
    const auto buffer_bytes = std::as_bytes(std::span(config.buffers));
    ASSERT_TRUE(writer.WriteBlob(buffer_bytes));
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
        ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span(entry.payload))));
      }
    }
    offset += region_size;

    footer.physics_resource_table = {
      .offset = offset,
      .count = static_cast<uint32_t>(descriptors.size()),
      .entry_size = sizeof(data::pak::physics::PhysicsResourceDesc),
    };
    const auto table_bytes = std::as_bytes(std::span(descriptors));
    ASSERT_TRUE(writer.WriteBlob(table_bytes));
    offset += table_bytes.size();
  }

  ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span(catalog_bytes))));
  footer.directory_offset = offset;
  footer.directory_size = 0U;
  footer.asset_count = 0U;
  footer.catalog_offset = offset;
  footer.catalog_size = catalog_bytes.size();
  ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span { &footer, 1U })));
}

} // namespace oxygen::content::testing
