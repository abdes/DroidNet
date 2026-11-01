//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

//! Hand-made plans for the `PakWriter_*_functional_test.cpp` files, which test
//! the writer against plans the planner would never make.
/*!
 A canonical plan is a format 11 plan, laid out as the planner lays one out:
 header, resource regions from the first payload boundary, each padded to one,
 resource tables, asset descriptors, directory, browse index, catalog, footer.
 The writer only moves forward, so a test moves only the section it is about,
 and the sources the plan names live in the fixture's in-memory workspace,
 where the writer also publishes the pak.
*/
namespace oxygen::content::pak::test {

using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SourceKeyOf;

inline constexpr auto kHeaderSize
  = uint32_t { sizeof(data::pak::core::PakHeader) };
inline constexpr auto kFooterSize
  = uint32_t { sizeof(data::pak::core::PakFooter) };
//! Where a payload starts and a region holding payloads ends.
inline constexpr auto kPayloadAlignment
  = uint64_t { data::pak::core::kPakPayloadAlignment };
//! The alignment the planner gives the tables and every section after them.
inline constexpr auto kSectionAlignment = uint64_t { 16U };

//! `value` rounded up to a multiple of the power of two `alignment`.
[[nodiscard]] constexpr auto AlignUp(
  const uint64_t value, const uint64_t alignment) -> uint64_t
{
  return (value + alignment - 1U) & ~(alignment - 1U);
}

//! The first payload boundary after the header, where the regions start.
inline constexpr auto kFirstPayloadOffset
  = AlignUp(kHeaderSize, kPayloadAlignment);

[[nodiscard]] inline auto MakeRegionPlan(std::string region_name,
  const uint64_t offset, const uint64_t size_bytes) -> PakRegionPlan
{
  return PakRegionPlan {
    .region_name = std::move(region_name),
    .offset = offset,
    .size_bytes = size_bytes,
    .alignment = static_cast<uint32_t>(kPayloadAlignment),
  };
}

//! The five resource regions from the first payload boundary; only the
//! texture region may hold payloads, in `texture_region_size` bytes.
[[nodiscard]] inline auto MakeCanonicalRegions(
  const uint64_t texture_region_size) -> std::vector<PakRegionPlan>
{
  const auto trailing_offset = kFirstPayloadOffset + texture_region_size;
  return {
    MakeRegionPlan("texture_region", kFirstPayloadOffset, texture_region_size),
    MakeRegionPlan("buffer_region", trailing_offset, 0U),
    MakeRegionPlan("audio_region", trailing_offset, 0U),
    MakeRegionPlan("script_region", trailing_offset, 0U),
    MakeRegionPlan("physics_region", trailing_offset, 0U),
  };
}

[[nodiscard]] inline auto MakeTablePlan(std::string table_name,
  const uint64_t offset, const uint32_t count, const size_t entry_size)
  -> PakTablePlan
{
  return PakTablePlan {
    .table_name = std::move(table_name),
    .offset = offset,
    .size_bytes = uint64_t { count } * entry_size,
    .count = count,
    .entry_size = static_cast<uint32_t>(entry_size),
    .expected_entry_size = static_cast<uint32_t>(entry_size),
    .alignment = static_cast<uint32_t>(kSectionAlignment),
    .index_zero_required = false,
    .index_zero_present = false,
    .index_zero_forbidden = false,
  };
}

//! The five resource tables from `offset`; only the texture table may hold
//! rows, `texture_count` of them.
[[nodiscard]] inline auto MakeCanonicalTables(const uint64_t offset,
  const uint32_t texture_count) -> std::vector<PakTablePlan>
{
  constexpr auto kTextureRowSize = sizeof(data::pak::core::TextureResourceDesc);
  const auto trailing_offset
    = offset + (uint64_t { texture_count } * kTextureRowSize);
  return {
    MakeTablePlan("texture_table", offset, texture_count, kTextureRowSize),
    MakeTablePlan("buffer_table", trailing_offset, 0U,
      sizeof(data::pak::core::BufferResourceDesc)),
    MakeTablePlan("audio_table", trailing_offset, 0U,
      sizeof(data::pak::audio::AudioResourceDesc)),
    MakeTablePlan("script_resource_table", trailing_offset, 0U,
      sizeof(data::pak::scripting::ScriptResourceDesc)),
    MakeTablePlan("physics_resource_table", trailing_offset, 0U,
      sizeof(data::pak::physics::PhysicsResourceDesc)),
  };
}

//! A texture placement at `offset` in the texture region.
[[nodiscard]] inline auto MakeTexturePlacement(
  const uint64_t offset, const uint64_t size_bytes) -> PakResourcePlacementPlan
{
  return PakResourcePlacementPlan {
    .resource_kind = "texture",
    .resource_index = 0U,
    .region_name = "texture_region",
    .offset = offset,
    .size_bytes = size_bytes,
    .alignment = 1U,
    .reserved_bytes_zeroed = true,
  };
}

//! The source key of every canonical plan, which the fixture's requests carry.
[[nodiscard]] inline auto CanonicalSourceKey() -> data::SourceKey
{
  constexpr auto kSeed = uint8_t { 0x7DU };
  return SourceKeyOf(kSeed);
}

//! The end of the last resource table, after which the asset descriptors go.
[[nodiscard]] inline auto TablesEnd(const PakPlan::Data& plan) -> uint64_t
{
  const auto& last = plan.tables.back();
  return last.offset + last.size_bytes;
}

//! Lays out, after the directory, the empty browse index, the catalog that
//! lists `entries`, and the footer, in that order and with the planner's
//! alignments; the file ends at the footer.
inline auto LayOutTail(
  PakPlan::Data& plan, std::vector<data::PakCatalogEntry> entries = {}) -> void
{
  auto catalog = data::PakCatalog {
    .source_key = plan.header.source_key,
    .content_version = plan.header.content_version,
    .catalog_digest = {},
    .mount = "Game",
    .entries = std::move(entries),
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();

  plan.browse_index = PakBrowseIndexPlan {
    .offset = AlignUp(
      plan.directory.offset + plan.directory.size_bytes, kSectionAlignment),
    .size_bytes = sizeof(data::pak::core::PakBrowseIndexHeader),
    .entries = {},
    .textures = {},
  };
  plan.catalog = PakCatalogPlan {
    .offset = plan.browse_index.offset + plan.browse_index.size_bytes,
    .bytes = catalog.Encode().value(),
  };
  const auto footer_offset = AlignUp(
    plan.catalog.offset + plan.catalog.bytes.size(), kSectionAlignment);
  plan.footer = PakFooterPlan {
    .offset = footer_offset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = footer_offset + offsetof(data::pak::core::PakFooter, pak_crc32),
  };
  plan.planned_file_size = footer_offset + kFooterSize;
}

//! Places the geometry `key`, whose descriptor is `descriptor`, after the
//! resource tables, lists it in the directory and the catalog, and lays the
//! directory and the tail out again after it.
/*!
 The plan names no source for the descriptor: a test adds one, or leaves it
 out to test a plan without one. The descriptor is at
 `plan.assets.front().offset`.
*/
inline auto AddGeometryAsset(PakPlan::Data& plan, const data::AssetKey& key,
  const std::span<const std::byte> descriptor) -> void
{
  const auto descriptor_offset = AlignUp(TablesEnd(plan), kSectionAlignment);
  plan.assets = {
    PakAssetPlacementPlan {
      .asset_key = key,
      .asset_type = data::AssetType::kGeometry,
      .offset = descriptor_offset,
      .size_bytes = descriptor.size(),
      .alignment = static_cast<uint32_t>(kSectionAlignment),
      .reserved_bytes_zeroed = true,
    },
  };
  plan.directory.offset
    = AlignUp(descriptor_offset + descriptor.size(), kSectionAlignment);
  plan.directory.size_bytes = sizeof(data::pak::core::AssetDirectoryEntry);
  plan.directory.entries = {
    PakAssetDirectoryEntryPlan {
      .asset_key = key,
      .asset_type = data::AssetType::kGeometry,
      .entry_offset = plan.directory.offset,
      .descriptor_offset = descriptor_offset,
      .descriptor_size = static_cast<uint32_t>(descriptor.size()),
    },
  };
  LayOutTail(plan,
    {
      {
        .asset_key = key,
        .asset_type = data::AssetType::kGeometry,
        .descriptor_digest = base::ComputeSha256(descriptor),
        .transitive_resource_digest = {},
      },
    });
}

//! Contents of `MakeCanonicalPlan`.
struct CanonicalPlanSpec final {
  uint16_t content_version = 1U;
  //! The size of the one texture payload the plan places; none when zero.
  uint64_t texture_payload_size = 0U;
};

//! A minimal format 11 plan, with the browse index and catalog every pak
//! holds, both empty.
/*!
 With a texture payload, the plan places it at the start of the texture
 region, pads the region to a payload boundary and lists one texture table
 row; a test adds the payload and row sources. Negative tests mutate the one
 field they are about.
*/
[[nodiscard]] inline auto MakeCanonicalPlan(const CanonicalPlanSpec& spec = {})
  -> PakPlan::Data
{
  const auto texture_region_size
    = AlignUp(spec.texture_payload_size, kPayloadAlignment);
  const auto texture_count = spec.texture_payload_size == 0U ? 0U : 1U;

  auto plan = PakPlan::Data {};
  plan.header = PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = spec.content_version,
    .source_key = CanonicalSourceKey(),
  };
  plan.regions = MakeCanonicalRegions(texture_region_size);
  plan.tables = MakeCanonicalTables(
    AlignUp(kFirstPayloadOffset + texture_region_size, kSectionAlignment),
    texture_count);
  if (texture_count != 0U) {
    plan.resources = {
      MakeTexturePlacement(kFirstPayloadOffset, spec.texture_payload_size),
    };
  }
  plan.directory = PakDirectoryPlan {
    .offset = AlignUp(TablesEnd(plan), kSectionAlignment),
    .size_bytes = 0U,
    .entries = {},
  };
  LayOutTail(plan);
  return plan;
}

//! An in-memory workspace that holds the sources a hand-made plan names and
//! the paks the writer publishes.
class PakWriterFixture : public ::testing::Test {
protected:
  [[nodiscard]] static auto CanonicalPlan(const CanonicalPlanSpec& spec = {})
    -> PakPlan::Data
  {
    return MakeCanonicalPlan(spec);
  }

  //! A request to write the pak `name` of `content_version`, its footer CRC
  //! computed or not.
  [[nodiscard]] auto Request(const uint16_t content_version,
    const std::string_view name = "out.pak", const bool crc = true)
    -> PakBuildRequest
  {
    auto request = workspace_.FullRequest({}, name);
    request.content_version = content_version;
    request.source_key = CanonicalSourceKey();
    request.options.compute_crc32 = crc;
    return request;
  }

  //! Adds the source file `name` holding `bytes`; returns the slice of all of
  //! it.
  auto AddSource(const std::string_view name,
    const std::span<const std::byte> bytes) -> PakPayloadSourceSlicePlan
  {
    auto slice = MissingSource(name, bytes.size());
    workspace_.Storage().AddFile(slice.source_path, bytes);
    return slice;
  }

  //! A slice of `size_bytes` of the source file `name`, which the workspace
  //! does not hold unless `AddSource` added it.
  [[nodiscard]] static auto MissingSource(const std::string_view name,
    const uint64_t size_bytes) -> PakPayloadSourceSlicePlan
  {
    return {
      .source_path = std::filesystem::path("/sources") / name,
      .source_offset = 0U,
      .size_bytes = size_bytes,
      .inline_bytes = {},
    };
  }

  //! Writes `plan` for `request`.
  [[nodiscard]] static auto Write(const PakBuildRequest& request,
    PakPlan::Data plan) -> PakWriter::WriteResult
  {
    return PakWriter {}.Write(request, PakPlan(std::move(plan)));
  }

  //! The bytes of the pak `request` published; empty when it published none.
  [[nodiscard]] auto Published(const PakBuildRequest& request)
    -> std::span<const std::byte>
  {
    return workspace_.Storage().Bytes(request.output_pak_path);
  }

private:
  PakWorkspace workspace_;
};

} // namespace oxygen::content::pak::test
