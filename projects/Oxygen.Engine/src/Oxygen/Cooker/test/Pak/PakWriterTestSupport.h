//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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
 A canonical plan lays its sections out in the order the writer writes them:
 header, resource regions, resource tables, asset descriptors, directory,
 browse index, catalog, footer. The writer only moves forward, so a test moves
 only the section it is about, and the sources the plan names live in the
 fixture's in-memory workspace, where the writer also publishes the pak.
*/
namespace oxygen::content::pak::test {

using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SourceKeyOf;

inline constexpr auto kHeaderSize
  = uint32_t { sizeof(data::pak::core::PakHeader) };
inline constexpr auto kFooterSize
  = uint32_t { sizeof(data::pak::core::PakFooter) };

struct RegionPlanSpec final {
  std::string region_name;
  uint64_t offset = 0U;
  uint64_t size_bytes = 0U;
  uint32_t alignment = 1U;
};

[[nodiscard]] inline auto MakeRegionPlan(RegionPlanSpec spec) -> PakRegionPlan
{
  return PakRegionPlan {
    .region_name = std::move(spec.region_name),
    .offset = spec.offset,
    .size_bytes = spec.size_bytes,
    .alignment = spec.alignment,
  };
}

struct TablePlanSpec final {
  std::string table_name;
  uint64_t offset = 0U;
  uint64_t size_bytes = 0U;
  uint32_t count = 0U;
  uint32_t entry_size = 0U;
};

[[nodiscard]] inline auto MakeTablePlan(TablePlanSpec spec) -> PakTablePlan
{
  return PakTablePlan {
    .table_name = std::move(spec.table_name),
    .offset = spec.offset,
    .size_bytes = spec.size_bytes,
    .count = spec.count,
    .entry_size = spec.entry_size,
    .expected_entry_size = spec.entry_size,
    .alignment = 1U,
    .index_zero_required = false,
    .index_zero_present = false,
    .index_zero_forbidden = false,
  };
}

struct CanonicalRegionSpec final {
  uint64_t base_offset = 0U;
  uint64_t texture_region_size = 0U;
};

//! The five resource regions; only the texture region may be non-empty.
[[nodiscard]] inline auto MakeCanonicalRegions(const CanonicalRegionSpec spec)
  -> std::vector<PakRegionPlan>
{
  const auto trailing_offset = spec.base_offset + spec.texture_region_size;
  return {
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "texture_region",
      .offset = spec.base_offset,
      .size_bytes = spec.texture_region_size,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "buffer_region",
      .offset = trailing_offset,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "audio_region",
      .offset = trailing_offset,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "script_region",
      .offset = trailing_offset,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "physics_region",
      .offset = trailing_offset,
    }),
  };
}

struct CanonicalTableSpec final {
  uint64_t texture_table_offset = 0U;
  uint32_t texture_count = 0U;
  uint64_t texture_table_size = 0U;
};

//! The five resource tables; only the texture table may be non-empty.
[[nodiscard]] inline auto MakeCanonicalTables(const CanonicalTableSpec spec)
  -> std::vector<PakTablePlan>
{
  const auto trailing_offset
    = spec.texture_table_offset + spec.texture_table_size;
  return {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = spec.texture_table_offset,
      .size_bytes = spec.texture_table_size,
      .count = spec.texture_count,
      .entry_size
      = static_cast<uint32_t>(sizeof(data::pak::core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = trailing_offset,
      .entry_size
      = static_cast<uint32_t>(sizeof(data::pak::core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = trailing_offset,
      .entry_size
      = static_cast<uint32_t>(sizeof(data::pak::audio::AudioResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = trailing_offset,
      .entry_size
      = static_cast<uint32_t>(sizeof(data::pak::scripting::ScriptResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = trailing_offset,
      .entry_size
      = static_cast<uint32_t>(sizeof(data::pak::physics::PhysicsResourceDesc)),
    }),
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

//! Lays out, right after the directory, the empty browse index, the catalog
//! that lists `entries`, and the footer, in that order; the file ends at the
//! footer.
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
    .offset = plan.directory.offset + plan.directory.size_bytes,
    .size_bytes = sizeof(data::pak::core::PakBrowseIndexHeader),
    .entries = {},
    .textures = {},
  };
  plan.catalog = PakCatalogPlan {
    .offset = plan.browse_index.offset + plan.browse_index.size_bytes,
    .bytes = catalog.Encode().value(),
  };
  const auto footer_offset = plan.catalog.offset + plan.catalog.bytes.size();
  plan.footer = PakFooterPlan {
    .offset = footer_offset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = footer_offset + offsetof(data::pak::core::PakFooter, pak_crc32),
  };
  plan.planned_file_size = footer_offset + kFooterSize;
}

//! Places the geometry `key`, whose descriptor is `descriptor`, at
//! `descriptor_offset`, lists it in the directory and the catalog, and lays
//! the tail out again after the grown directory.
/*!
 The plan names no source for the descriptor: a test adds one, or leaves it
 out to test a plan without one.
*/
inline auto AddGeometryAsset(PakPlan::Data& plan, const data::AssetKey& key,
  const uint64_t descriptor_offset, const std::span<const std::byte> descriptor)
  -> void
{
  plan.assets = {
    PakAssetPlacementPlan {
      .asset_key = key,
      .asset_type = data::AssetType::kGeometry,
      .offset = descriptor_offset,
      .size_bytes = descriptor.size(),
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
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
    { {
      .asset_key = key,
      .asset_type = data::AssetType::kGeometry,
      .descriptor_digest = base::ComputeSha256(descriptor),
      .transitive_resource_digest = {},
    } });
}

//! Layout knobs for `MakeCanonicalPlan`.
/*!
 Regions start at `base_offset`, and the tables too unless `tables_offset`
 says otherwise. The directory follows the tables unless `directory_offset`
 says otherwise; the browse index, the catalog and the footer follow it.
*/
struct CanonicalPlanSpec final {
  uint16_t content_version = 1U;
  uint64_t base_offset = 256U;
  uint64_t texture_region_size = 0U;
  std::optional<uint64_t> tables_offset;
  std::optional<uint64_t> directory_offset;
  uint32_t texture_count = 0U;
  uint64_t texture_table_size = 0U;
};

//! A minimal plan with no resources or assets, and the browse index and
//! catalog every pak holds, both empty.
/*!
 Negative tests mutate the one field they are about.
*/
[[nodiscard]] inline auto MakeCanonicalPlan(const CanonicalPlanSpec& spec = {})
  -> PakPlan::Data
{
  const auto tables_offset = spec.tables_offset.value_or(spec.base_offset);

  auto plan = PakPlan::Data {};
  plan.header = PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = spec.content_version,
    .source_key = CanonicalSourceKey(),
  };
  plan.regions = MakeCanonicalRegions(CanonicalRegionSpec {
    .base_offset = spec.base_offset,
    .texture_region_size = spec.texture_region_size,
  });
  plan.tables = MakeCanonicalTables(CanonicalTableSpec {
    .texture_table_offset = tables_offset,
    .texture_count = spec.texture_count,
    .texture_table_size = spec.texture_table_size,
  });
  plan.directory = PakDirectoryPlan {
    .offset
    = spec.directory_offset.value_or(tables_offset + spec.texture_table_size),
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
