//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/Reader.h>

#include <span>

namespace oxygen::serio {

//! Load specialization for IndexHeader.
inline auto Load(AnyReader& reader, data::loose_cooked::v1::IndexHeader& header)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::byte*>(header.magic), std::size(header.magic))));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.content_version));
  CHECK_RESULT(reader.ReadInto(header.flags));

  CHECK_RESULT(reader.ReadInto(header.string_table_offset));
  CHECK_RESULT(reader.ReadInto(header.string_table_size));

  CHECK_RESULT(reader.ReadInto(header.asset_entries_offset));
  CHECK_RESULT(reader.ReadInto(header.asset_count));
  CHECK_RESULT(reader.ReadInto(header.asset_entry_size));

  CHECK_RESULT(reader.ReadInto(header.file_records_offset));
  CHECK_RESULT(reader.ReadInto(header.file_record_count));
  CHECK_RESULT(reader.ReadInto(header.file_record_size));

  CHECK_RESULT(reader.Forward(std::size(header.reserved)));
  return {};
}

//! Load specialization for AssetEntry.
inline auto Load(AnyReader& reader, data::loose_cooked::v1::AssetEntry& entry)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(entry.asset_key));
  CHECK_RESULT(reader.ReadInto(entry.descriptor_relpath_offset));
  CHECK_RESULT(reader.ReadInto(entry.virtual_path_offset));
  CHECK_RESULT(reader.ReadInto(entry.asset_type));
  CHECK_RESULT(reader.Forward(std::size(entry.reserved0)));
  CHECK_RESULT(reader.ReadInto(entry.descriptor_size));
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::byte*>(entry.descriptor_sha256),
    std::size(entry.descriptor_sha256))));
  CHECK_RESULT(reader.Forward(std::size(entry.reserved1)));
  return {};
}

//! Load specialization for FileRecord.
inline auto Load(AnyReader& reader, data::loose_cooked::v1::FileRecord& record)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  uint16_t kind_u = 0;
  CHECK_RESULT(reader.ReadInto(kind_u));
  using data::loose_cooked::v1::FileKind;
  switch (static_cast<FileKind>(kind_u)) {
  case FileKind::kUnknown:
  case FileKind::kBuffersTable:
  case FileKind::kBuffersData:
  case FileKind::kTexturesTable:
  case FileKind::kTexturesData:
    record.kind = static_cast<FileKind>(kind_u);
    break;
  default:
    return std::make_error_code(std::errc::invalid_argument);
  }
  CHECK_RESULT(reader.ReadInto(record.reserved0));
  CHECK_RESULT(reader.ReadInto(record.relpath_offset));
  CHECK_RESULT(reader.ReadInto(record.size));
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::byte*>(record.sha256), std::size(record.sha256))));
  CHECK_RESULT(reader.Forward(std::size(record.reserved1)));
  return {};
}

} // namespace oxygen::serio
