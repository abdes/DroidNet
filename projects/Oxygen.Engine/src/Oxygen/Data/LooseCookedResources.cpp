//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::data::loose_cooked {

using data::CookedDefect;
using data::CookedFormatError;

namespace {
  template <class Desc>
  auto ReadRows(const std::span<const std::byte> rows)
    -> std::vector<ResourcePayload>
  {
    if (rows.size() % sizeof(Desc) != 0U) {
      throw CookedFormatError(
        CookedDefect::kWrongRowSize, "the resource table holds a partial row");
    }
    auto payloads = std::vector<ResourcePayload> {};
    const auto count = rows.size() / sizeof(Desc);
    for (size_t row = 0; row < count; ++row) {
      const auto desc
        = serio::CopyAt<Desc>(rows.subspan(row * sizeof(Desc), sizeof(Desc)));
      if (!desc) {
        throw CookedFormatError(CookedDefect::kWrongRowSize,
          "the resource table holds a partial row");
      }
      if constexpr (std::is_same_v<Desc, pak::core::TextureResourceDesc>) {
        if (!pak::core::HasSupportedCompression(*desc)) {
          throw CookedFormatError(CookedDefect::kInvalidValue,
            "a texture row records a compression its format lacks");
        }
      }
      if (desc->size_bytes == 0U) {
        continue;
      }
      if (base::IsAllZero(desc->content_hash)) {
        throw CookedFormatError(
          CookedDefect::kMissingDigest, "a resource row has no content digest");
      }
      payloads.push_back({
        .row = static_cast<uint32_t>(row),
        .offset = desc->data_offset,
        .size = desc->size_bytes,
        .sha256 = desc->content_hash,
      });
    }
    return payloads;
  }
} // namespace

auto IsResourceDataFile(const FileKind kind) noexcept -> bool
{
  return kind == FileKind::kBuffersData || kind == FileKind::kTexturesData
    || kind == FileKind::kScriptsData || kind == FileKind::kPhysicsData;
}

auto DataFileKindOf(const FileKind table) noexcept -> std::optional<FileKind>
{
  switch (table) {
  case FileKind::kBuffersTable:
    return FileKind::kBuffersData;
  case FileKind::kTexturesTable:
    return FileKind::kTexturesData;
  case FileKind::kScriptsTable:
    return FileKind::kScriptsData;
  case FileKind::kPhysicsTable:
    return FileKind::kPhysicsData;
  default:
    return std::nullopt;
  }
}

auto ResourceRowSize(const FileKind table) -> size_t
{
  switch (table) {
  case FileKind::kBuffersTable:
    return sizeof(pak::core::BufferResourceDesc);
  case FileKind::kTexturesTable:
    return sizeof(pak::core::TextureResourceDesc);
  case FileKind::kScriptsTable:
    return sizeof(pak::scripting::ScriptResourceDesc);
  case FileKind::kPhysicsTable:
    return sizeof(pak::physics::PhysicsResourceDesc);
  default:
    throw std::invalid_argument("not a resource table kind");
  }
}

auto IsResourceTableSize(const FileKind table, const uint64_t size) -> bool
{
  const auto row_size = ResourceRowSize(table);
  const auto holds_whole_rows = size % row_size == 0U;
  const auto rows_fit_an_index
    = size / row_size <= std::numeric_limits<uint32_t>::max();
  return holds_whole_rows && rows_fit_an_index;
}

auto ReadResourcePayloads(const FileKind table,
  const std::span<const std::byte> rows) -> std::vector<ResourcePayload>
{
  switch (table) {
  case FileKind::kBuffersTable:
    return ReadRows<pak::core::BufferResourceDesc>(rows);
  case FileKind::kTexturesTable:
    return ReadRows<pak::core::TextureResourceDesc>(rows);
  case FileKind::kScriptsTable:
    return ReadRows<pak::scripting::ScriptResourceDesc>(rows);
  case FileKind::kPhysicsTable:
    return ReadRows<pak::physics::PhysicsResourceDesc>(rows);
  default:
    throw std::invalid_argument("not a resource table kind");
  }
}

} // namespace oxygen::data::loose_cooked
