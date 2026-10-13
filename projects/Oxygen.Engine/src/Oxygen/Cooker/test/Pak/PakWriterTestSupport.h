//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>

//! Shared plan-building helpers for the `PakWriter_*_functional_test.cpp`
//! files.
namespace oxygen::content::pak::test {

inline constexpr auto kHeaderSize
  = uint32_t { sizeof(data::pak::core::PakHeader) };
inline constexpr auto kFooterSize
  = uint32_t { sizeof(data::pak::core::PakFooter) };

//! Deterministic bytes `start_value, start_value + 1, ...` (wrapping).
[[nodiscard]] inline auto MakePatternBytes(
  const uint8_t start_value, const size_t byte_count) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte> {};
  bytes.reserve(byte_count);
  for (size_t i = 0; i < byte_count; ++i) {
    const auto value
      = static_cast<uint8_t>(start_value + static_cast<uint8_t>(i));
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

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

//! Places one geometry descriptor and the directory entry that points at it.
/*!
 The directory offset must already be set (see `CanonicalPlanSpec`).
*/
inline auto AddGeometryAsset(PakPlan::Data& plan, const data::AssetKey& key,
  const uint64_t descriptor_offset, const uint64_t descriptor_size) -> void
{
  plan.assets = {
    PakAssetPlacementPlan {
      .asset_key = key,
      .asset_type = data::AssetType::kGeometry,
      .offset = descriptor_offset,
      .size_bytes = descriptor_size,
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
      .descriptor_size = static_cast<uint32_t>(descriptor_size),
    },
  };
}

//! Layout knobs for `PakWriterFixture::CanonicalPlan`.
/*!
 Regions start at `base_offset`. The tables, the directory and the footer
 default to `base_offset`; the directory defaults to the footer offset.
*/
struct CanonicalPlanSpec final {
  uint16_t content_version = 1U;
  uint64_t base_offset = 256U;
  uint64_t texture_region_size = 0U;
  std::optional<uint64_t> tables_offset;
  std::optional<uint64_t> directory_offset;
  std::optional<uint64_t> footer_offset;
  uint32_t texture_count = 0U;
  uint64_t texture_table_size = 0U;
};

//! Per-test temp directory plus a valid minimal plan to mutate.
class PakWriterFixture : public TempDirFixture {
protected:
  [[nodiscard]] static auto WriterSourceKey() -> data::SourceKey
  {
    return MakeSourceKey(0x7DU);
  }

  //! A valid minimal plan with no resources, assets or catalog.
  /*!
   Negative tests mutate the one field they are about.
  */
  [[nodiscard]] static auto CanonicalPlan(const CanonicalPlanSpec& spec = {})
    -> PakPlan::Data
  {
    const auto footer_offset = spec.footer_offset.value_or(spec.base_offset);
    const auto directory_offset = spec.directory_offset.value_or(footer_offset);

    auto plan = PakPlan::Data {};
    plan.header = PakHeaderPlan {
      .offset = 0U,
      .size_bytes = kHeaderSize,
      .content_version = spec.content_version,
      .source_key = WriterSourceKey(),
    };
    plan.regions = MakeCanonicalRegions(CanonicalRegionSpec {
      .base_offset = spec.base_offset,
      .texture_region_size = spec.texture_region_size,
    });
    plan.tables = MakeCanonicalTables(CanonicalTableSpec {
      .texture_table_offset = spec.tables_offset.value_or(spec.base_offset),
      .texture_count = spec.texture_count,
      .texture_table_size = spec.texture_table_size,
    });
    plan.directory = PakDirectoryPlan {
      .offset = directory_offset,
      .size_bytes = 0U,
      .entries = {},
    };
    plan.browse_index = PakBrowseIndexPlan {
      .enabled = false,
      .offset = 0U,
      .size_bytes = 0U,
      .entries = {},
    };
    plan.footer = PakFooterPlan {
      .offset = footer_offset,
      .size_bytes = kFooterSize,
      .crc32_field_absolute_offset
      = footer_offset + offsetof(data::pak::core::PakFooter, pak_crc32),
    };
    plan.planned_file_size = footer_offset + kFooterSize;
    return plan;
  }
};

} // namespace oxygen::content::pak::test
