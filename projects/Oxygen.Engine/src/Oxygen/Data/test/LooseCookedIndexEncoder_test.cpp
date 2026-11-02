//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::AssetEntry;
using oxygen::data::loose_cooked::EncodeLooseCookedIndex;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::loose_cooked::FileRecord;
using oxygen::data::loose_cooked::IndexContent;
using oxygen::data::loose_cooked::IndexedAsset;
using oxygen::data::loose_cooked::IndexedFile;
using oxygen::data::loose_cooked::IndexHeader;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteReader;

auto DigestOf(const std::string_view text) -> oxygen::base::Sha256Digest
{
  return oxygen::base::ComputeSha256(std::as_bytes(std::span(text)));
}

auto Material(const std::string_view virtual_path) -> IndexedAsset
{
  const auto relpath = std::string(virtual_path.substr(1));
  return {
    .key = AssetKey::FromVirtualPath(virtual_path),
    .asset_type = AssetType::kMaterial,
    .virtual_path = std::string(virtual_path),
    .descriptor_relpath = relpath,
    .descriptor_size = relpath.size(),
    .descriptor_sha256 = DigestOf(relpath),
    .references = {},
    .source_path = "/Content/Ship.glb",
  };
}

//! A resource data file carries no digest: each row of its table does. A
//! resource table holds one row.
auto File(const FileKind kind, const std::string_view relpath) -> IndexedFile
{
  const auto is_table
    = oxygen::data::loose_cooked::DataFileKindOf(kind).has_value();
  return {
    .kind = kind,
    .relpath = std::string(relpath),
    .size = is_table ? oxygen::data::loose_cooked::ResourceRowSize(kind)
                     : relpath.size(),
    .sha256 = oxygen::data::loose_cooked::IsResourceDataFile(kind)
      ? oxygen::base::Sha256Digest {}
      : DigestOf(relpath),
    .virtual_path = {},
    .source_path = {},
  };
}

auto Content(const std::span<const IndexedAsset> assets,
  const std::span<const IndexedFile> files) -> IndexContent
{
  return {
    .source_key = oxygen::data::SourceKey { oxygen::Uuid::Generate() },
    .content_version = 3,
    .assets = assets,
    .files = files,
  };
}

//! The sections follow one another as the header names them, and every
//! string is stored once.
NOLINT_TEST(LooseCookedIndexEncoderTest, SectionsFollowTheHeader)
{
  // Arrange
  const auto assets = std::array {
    Material("/Content/B.omat"),
    Material("/Content/A.omat"),
  };
  const auto files = std::array {
    File(FileKind::kBuffersTable, "buffers.table"),
    File(FileKind::kBuffersData, "buffers.data"),
  };
  auto stream = oxygen::serio::MemoryStream {};
  auto writer = oxygen::serio::Writer(stream);

  // Act
  const auto encoded = EncodeLooseCookedIndex(Content(assets, files), writer);

  // Assert
  ASSERT_TRUE(encoded.has_value());
  auto reader = ByteReader(stream.Data());
  const auto header = reader.Read<IndexHeader>();
  ASSERT_HAS_VALUE(header);
  EXPECT_EQ(header->content_version, 3U);
  EXPECT_EQ(header->string_table_offset, sizeof(IndexHeader));
  // The leading NUL, then 2 x (relpath, virtual path) and one shared source,
  // then the 2 file paths, each NUL-terminated.
  const auto expected_strings = 1U + (2U * ((14U + 1U) + (15U + 1U)))
    + (17U + 1U) + (13U + 1U) + (12U + 1U);
  EXPECT_EQ(header->string_table_size, expected_strings);
  EXPECT_EQ(header->asset_entries_offset,
    header->string_table_offset + header->string_table_size);
  EXPECT_EQ(header->file_records_offset,
    header->asset_entries_offset + (2U * sizeof(AssetEntry)));
  EXPECT_EQ(stream.Data().size(),
    header->file_records_offset + (2U * sizeof(FileRecord)));
  ASSERT_TRUE(reader.Seek(ByteOffset { header->asset_entries_offset }));
  const auto first = reader.Read<AssetEntry>();
  ASSERT_HAS_VALUE(first);
  // Assets are written in key order.
  EXPECT_EQ(first->asset_key, std::min(assets[0].key, assets[1].key));
}

//! Content the format cannot hold is refused before anything is written.
NOLINT_TEST(LooseCookedIndexEncoderTest, ContentTheFormatCannotHoldIsRefused)
{
  // Arrange
  const auto a = Material("/Content/A.omat");
  auto same_path = Material("/Content/B.omat");
  same_path.virtual_path = a.virtual_path;
  auto same_file = Material("/Content/B.omat");
  same_file.descriptor_relpath = a.descriptor_relpath;
  auto no_digest = Material("/Content/B.omat");
  no_digest.descriptor_sha256 = {};
  const auto table = File(FileKind::kBuffersTable, "buffers.table");
  auto second_table = File(FileKind::kBuffersTable, "more.table");
  auto unknown = File(FileKind::kUnknown, "unknown.bin");
  auto digested_data = File(FileKind::kBuffersData, "buffers.data");
  digested_data.sha256 = DigestOf(digested_data.relpath);
  auto undigested_aux = File(FileKind::kAuxiliary, "Ship/Hull.obuf");
  undigested_aux.sha256 = {};

  struct Case {
    std::string_view name;
    std::array<IndexedAsset, 2> assets;
    std::array<IndexedFile, 2> files;
  };
  const auto aux = File(FileKind::kAuxiliary, "Ship/Hull.obuf");
  const auto aux2 = File(FileKind::kAuxiliary, "Ship/Deck.obuf");
  const auto b = Material("/Content/B.omat");
  auto no_source = Material("/Content/B.omat");
  no_source.source_path = {};
  auto sourceless_texture = File(FileKind::kTextureDescriptor, "Sky.otex");
  sourceless_texture.virtual_path = "/Content/Sky.otex";
  auto unnamed_texture = File(FileKind::kTextureDescriptor, "Sky.otex");
  unnamed_texture.source_path = "/Content/Sky.png";
  auto named_table = File(FileKind::kBuffersTable, "buffers.table");
  named_table.virtual_path = "/Content/buffers.table";
  auto sourced_table = File(FileKind::kBuffersTable, "buffers.table");
  sourced_table.source_path = "/Content/Ship.glb";
  const auto escaping = File(FileKind::kAuxiliary, "../Hull.obuf");
  const auto cases = std::array {
    Case { .name = "key", .assets = { a, a }, .files = { aux, aux2 } },
    Case {
      .name = "virtual path",
      .assets = { a, same_path },
      .files = { aux, aux2 },
    },
    Case {
      .name = "content path",
      .assets = { a, same_file },
      .files = { aux, aux2 },
    },
    Case {
      .name = "digest",
      .assets = { a, no_digest },
      .files = { aux, aux2 },
    },
    Case {
      .name = "data file digest",
      .assets = { a, b },
      .files = { table, digested_data },
    },
    Case {
      .name = "file digest",
      .assets = { a, b },
      .files = { undigested_aux, aux2 },
    },
    Case {
      .name = "kind",
      .assets = { a, b },
      .files = { table, second_table },
    },
    Case {
      .name = "unknown kind",
      .assets = { a, b },
      .files = { aux, unknown },
    },
    Case { .name = "file path", .assets = { a, b }, .files = { aux, aux } },
    Case {
      .name = "asset source",
      .assets = { a, no_source },
      .files = { aux, aux2 },
    },
    Case {
      .name = "texture source",
      .assets = { a, b },
      .files = { aux, sourceless_texture },
    },
    Case {
      .name = "texture virtual path",
      .assets = { a, b },
      .files = { aux, unnamed_texture },
    },
    Case {
      .name = "table virtual path",
      .assets = { a, b },
      .files = { aux, named_table },
    },
    Case {
      .name = "table source",
      .assets = { a, b },
      .files = { aux, sourced_table },
    },
    Case {
      .name = "escaping path",
      .assets = { a, b },
      .files = { aux, escaping },
    },
  };

  for (const auto& test_case : cases) {
    auto stream = oxygen::serio::MemoryStream {};
    auto writer = oxygen::serio::Writer(stream);

    // Act & Assert
    EXPECT_THROW(static_cast<void>(EncodeLooseCookedIndex(
                   Content(test_case.assets, test_case.files), writer)),
      std::invalid_argument)
      << test_case.name;
    EXPECT_TRUE(stream.Data().empty()) << test_case.name;
  }
}

//! Equal content encodes to equal bytes, whatever the order of its assets
//! and files.
NOLINT_TEST(LooseCookedIndexEncoderTest, EqualContentInAnyOrderEncodesEqually)
{
  // Arrange
  const auto source_key = oxygen::data::SourceKey { oxygen::Uuid::Generate() };
  const auto encode
    = [&source_key](const std::span<const IndexedAsset> assets,
        const std::span<const IndexedFile> files) -> std::vector<std::byte> {
    auto stream = oxygen::serio::MemoryStream {};
    auto writer = oxygen::serio::Writer(stream);
    auto content = Content(assets, files);
    content.source_key = source_key;
    EXPECT_TRUE(EncodeLooseCookedIndex(content, writer).has_value());
    const auto bytes = stream.Data();
    return { bytes.begin(), bytes.end() };
  };
  const auto a = Material("/Content/A.omat");
  const auto b = Material("/Content/B.omat");
  const auto table = File(FileKind::kBuffersTable, "buffers.table");
  const auto data = File(FileKind::kBuffersData, "buffers.data");

  // Act
  const auto forward = encode(std::array { a, b }, std::array { table, data });
  const auto reversed = encode(std::array { b, a }, std::array { data, table });

  // Assert
  EXPECT_EQ(forward, reversed);
}

//! A failing stream fails the encoding with the stream's error.
NOLINT_TEST(LooseCookedIndexEncoderTest, StreamErrorsAreReturned)
{
  using Device
    = oxygen::serio::testing::FaultInjectingDevice<oxygen::serio::MemoryFile>;

  // Arrange
  const auto assets = std::array { Material("/Content/A.omat") };
  auto device = Device { oxygen::serio::MemoryFile {} };
  device.LimitCapacity(ByteCount { sizeof(IndexHeader) });
  auto stream = oxygen::serio::RegionWriteStream<Device> {
    device,
    ByteOffset { 0 },
    ByteCount { 0 },
  };
  auto writer = oxygen::serio::Writer(stream);

  // Act
  const auto encoded = EncodeLooseCookedIndex(Content(assets, {}), writer);

  // Assert
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error(), oxygen::serio::Error::kNoSpace);
}

} // namespace
