//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ResourceTableRegistry.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/HoldingStorage.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {
namespace co = oxygen::co;
namespace imp = oxygen::content::import;

using oxygen::cooker::testing::HoldingStorage;

//! The buffer row `MakeBufferRow` builds: rows of another `tag` or size are
//! other resources. A row of a payload has a size and a digest.
struct BufferRowSpec final {
  uint8_t tag = 1U;
  uint32_t size_bytes = 1U;
};

auto MakeBufferRow(const BufferRowSpec& spec)
  -> oxygen::data::pak::core::BufferResourceDesc
{
  auto row = oxygen::data::pak::core::BufferResourceDesc {};
  row.size_bytes = spec.size_bytes;
  row.content_hash.at(0) = spec.tag;
  return row;
}

namespace lc = oxygen::data::loose_cooked;

//! The record an index lists for the file `relpath` holding `bytes`; a data
//! file has no digest of its own.
auto RecordOf(const lc::FileKind kind, std::string relpath,
  const std::span<const std::byte> bytes) -> lc::IndexedFile
{
  return {
    .kind = kind,
    .relpath = std::move(relpath),
    .size = bytes.size(),
    .sha256 = lc::IsResourceDataFile(kind) ? oxygen::base::Sha256Digest {}
                                           : oxygen::base::ComputeSha256(bytes),
  };
}

//! The record of an empty buffers data file, which a buffers table pairs.
auto EmptyBuffersData() -> lc::IndexedFile
{
  return RecordOf(lc::FileKind::kBuffersData,
    std::string(imp::internal::loose_layout::kBuffersData), {});
}

//! The bytes of the index of a commit that listed `files`: the way a table or
//! data file a root already holds is found.
auto EncodeIndex(const std::span<const lc::IndexedFile> files)
  -> std::vector<std::byte>
{
  auto stream = oxygen::serio::MemoryStream {};
  auto writer = oxygen::serio::Writer(stream);
  const auto encoded = lc::EncodeLooseCookedIndex(
    {
      .source_key = oxygen::data::SourceKey { oxygen::Uuid::Generate() },
      .files = files,
    },
    writer);
  if (!encoded) {
    throw std::system_error(encoded.error(), "encode the loose index");
  }
  return { stream.Data().begin(), stream.Data().end() };
}

NOLINT_TEST(ResourceTableRegistryTest, TableAdmissionWaitsForCompletedOverwrite)
{
  const oxygen::testing::ScopedTempDir temp;
  imp::ImportEventLoop loop;
  // Only the publication of a table is held in flight.
  HoldingStorage storage(oxygen::content::cooked::RealCookerStorage(),
    [](const std::filesystem::path& path) -> bool {
      return path.extension() == ".table";
    });
  imp::ResourceTableRegistry tables(loop.Driver(),
    oxygen::make_observer<oxygen::content::cooked::ImportStorage>(&storage));
  const auto root = temp.Path() / "oxygen_table_admission";
  auto first = tables.BeginSession(root);
  const auto inserted
    = tables.BufferAggregator(root).Acquire(MakeBufferRow({ .tag = 1U }), 1U);
  uint32_t second_index = 0;
  bool admitted = false;
  co::Event attempting;
  co::Event finished;
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&] -> co::Co<> {
        EXPECT_TRUE((co_await tables.EndSession(first)).ok);
      });
      co_await storage.publication_held;
      EXPECT_FALSE(first.IsActive());
      EXPECT_THROW(
        static_cast<void>(tables.BeginSession(root)), std::logic_error);
      tasks.Start([&] -> co::Co<> {
        attempting.Trigger();
        co_await tables.WaitForFinalization(root);
        admitted = true;
        // The first session's table is on disk; a commit lists it, as the
        // import that owns the root does, so the next session loads it.
        const auto table_path = oxygen::cooker::test::CookedFile(
          root, imp::internal::loose_layout::kBuffersTable);
        const auto table_bytes = oxygen::cooker::test::ReadBytes(table_path);
        const auto listed = std::array {
          RecordOf(lc::FileKind::kBuffersTable,
            table_path.lexically_relative(root).generic_string(), table_bytes),
          EmptyBuffersData(),
        };
        oxygen::cooker::test::WriteBytes(
          root / std::string(lc::kIndexFileName), EncodeIndex(listed));
        auto second = tables.BeginSession(root);
        second_index = tables.BufferAggregator(root)
                         .Acquire(MakeBufferRow({ .tag = 2U }), 1U)
                         .index;
        EXPECT_TRUE((co_await tables.EndSession(second)).ok);
        finished.Trigger();
      });
      co_await attempting;
      EXPECT_FALSE(admitted);
      storage.Release();
      co_await finished;
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(admitted);
  EXPECT_EQ(second_index, inserted.index + 1U);
}

//! A table the cooked root lists but that cannot be read as rows fails the
//! import: starting it empty would publish a table that drops the root's
//! existing resources.
NOLINT_TEST(ResourceTableRegistryTest, UnreadableExistingTableFailsTheImport)
{
  // Arrange
  imp::ImportEventLoop loop;
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  const auto root = oxygen::content::testing::MemoryRoot() / "broken_table";
  // One byte: not a whole number of buffer rows, though the index records
  // the table of one row the commit wrote.
  const auto table_bytes = std::array { std::byte { 1U } };
  auto table = RecordOf(lc::FileKind::kBuffersTable,
    lc::ContentRelPath(imp::internal::loose_layout::kBuffersTable,
      oxygen::base::ComputeSha256(table_bytes)),
    table_bytes);
  table.size = lc::ResourceRowSize(lc::FileKind::kBuffersTable);
  const auto listed = std::array { table, EmptyBuffersData() };
  const auto table_path = root / listed.front().relpath;
  storage.AddFile(table_path, table_bytes);
  storage.AddFile(root / std::string(lc::kIndexFileName), EncodeIndex(listed));
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));

  // Act + Assert
  EXPECT_THROW(
    static_cast<void>(tables.BufferAggregator(root)), imp::ResourceTableError);
  EXPECT_EQ(storage.SizeOf(table_path), oxygen::serio::ByteCount { 1U });
}

//! The last session of a cohort writes the tables and reports the content
//! name and digest of each as it wrote it; the sessions before it write
//! nothing.
NOLINT_TEST(ResourceTableRegistryTest, LastSessionReportsTheTablesItWrote)
{
  // Arrange
  const oxygen::testing::ScopedTempDir temp;
  imp::ImportEventLoop loop;
  auto& storage = oxygen::content::cooked::RealCookerStorage();
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));
  const auto root = temp.Path() / "oxygen_table_digests";
  auto first = tables.BeginSession(root);
  auto second = tables.BeginSession(root);
  static_cast<void>(
    tables.BufferAggregator(root).Acquire(MakeBufferRow({ .tag = 1U }), 1U));

  // Act
  auto first_end = imp::ResourceTableRegistry::SessionEnd {};
  auto second_end = imp::ResourceTableRegistry::SessionEnd {};
  co::Run(loop, [&] -> co::Co<> {
    first_end = co_await tables.EndSession(first);
    second_end = co_await tables.EndSession(second);
  });

  // Assert
  EXPECT_TRUE(first_end.ok);
  EXPECT_TRUE(first_end.written.empty());
  ASSERT_TRUE(second_end.ok);
  ASSERT_EQ(second_end.written.size(), 1U);
  const auto& table = second_end.written.front();
  EXPECT_EQ(table.kind, oxygen::data::loose_cooked::FileKind::kBuffersTable);
  EXPECT_EQ(oxygen::cooker::test::LayoutPathOf(table.relpath),
    imp::internal::loose_layout::kBuffersTable);
  const auto bytes = oxygen::content::cooked::ReadWholeFile(
    storage, root / table.relpath, oxygen::content::cooked::kMaxMetadataBytes);
  ASSERT_HAS_VALUE(bytes);
  EXPECT_EQ(table.digest.size, bytes->size());
  EXPECT_EQ(table.digest.sha256, oxygen::base::ComputeSha256(*bytes));
}

//! The data file is opened once and never truncated: ranges written through it
//! by two holders of the aggregator both land, and bytes already in the file
//! outside them stay.
NOLINT_TEST(ResourceTableRegistryTest, ReservedRangesWrittenThroughOneDataFile)
{
  // Arrange
  imp::ImportEventLoop loop;
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  const auto root = oxygen::content::testing::MemoryRoot() / "ranges";
  const auto data_path = root / imp::internal::loose_layout::kBuffersData;
  const auto committed = std::array { std::byte { 0xAA }, std::byte { 0xBB } };
  storage.AddFile(data_path, committed);
  // The index lists the data file at the length the commit holds: new
  // payloads go after it.
  // The table of the commit holds the reserved empty row.
  const auto sentinel
    = std::vector<std::byte>(lc::ResourceRowSize(lc::FileKind::kBuffersTable));
  const auto table = RecordOf(lc::FileKind::kBuffersTable,
    lc::ContentRelPath(imp::internal::loose_layout::kBuffersTable,
      oxygen::base::ComputeSha256(sentinel)),
    sentinel);
  storage.AddFile(root / table.relpath, sentinel);
  const auto listed = std::array {
    table,
    RecordOf(lc::FileKind::kBuffersData,
      std::string(imp::internal::loose_layout::kBuffersData), committed),
  };
  storage.AddFile(root / std::string(lc::kIndexFileName), EncodeIndex(listed));
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));
  auto& aggregator = tables.BufferAggregator(root);
  const auto first
    = aggregator.Acquire(MakeBufferRow({ .tag = 1U, .size_bytes = 3U }), 1U)
        .reservation;
  const auto second
    = aggregator.Acquire(MakeBufferRow({ .tag = 1U, .size_bytes = 2U }), 1U)
        .reservation;
  const auto one
    = std::array { std::byte { 1U }, std::byte { 2U }, std::byte { 3U } };
  const auto two = std::array { std::byte { 4U }, std::byte { 5U } };

  // Act
  const auto file = aggregator.DataFile();
  ASSERT_HAS_VALUE(file);
  const auto reopened = aggregator.DataFile();
  co::Run(loop, [&] -> co::Co<> {
    // Out of reservation order, as concurrent emitters complete.
    EXPECT_TRUE(co_await (*file)->AsyncWriteAt(
      oxygen::serio::ByteOffset { second.aligned_offset }, two));
    EXPECT_TRUE(co_await (*file)->AsyncWriteAt(
      oxygen::serio::ByteOffset { first.aligned_offset }, one));
  });

  // Assert
  ASSERT_HAS_VALUE(reopened);
  EXPECT_EQ(*reopened, *file);
  const auto bytes = storage.Bytes(data_path);
  ASSERT_EQ(bytes.size(), 7U);
  EXPECT_EQ(bytes[0], std::byte { 0xAA });
  EXPECT_EQ(bytes[1], std::byte { 0xBB });
  EXPECT_EQ(bytes[2], std::byte { 1U });
  EXPECT_EQ(bytes[4], std::byte { 3U });
  EXPECT_EQ(bytes[5], std::byte { 4U });
  EXPECT_EQ(bytes[6], std::byte { 5U });
}

//! A table that cannot be published fails the session that finalizes it, and
//! the failure is the storage's error.
NOLINT_TEST(ResourceTableRegistryTest, TableThatCannotBePublishedFailsTheEnd)
{
  // Arrange
  imp::ImportEventLoop loop;
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  const auto root = oxygen::content::testing::MemoryRoot() / "unpublishable";
  storage.AddFolder(root);
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));
  auto session = tables.BeginSession(root);
  const auto acquired
    = tables.BufferAggregator(root).Acquire(MakeBufferRow({ .tag = 1U }), 1U);
  // The table is published under its content name, which the rows decide:
  // the sentinel and the row acquired above.
  auto rows = std::array {
    oxygen::data::pak::core::BufferResourceDesc {},
    MakeBufferRow({ .tag = 1U }),
  };
  rows.at(1).data_offset = acquired.reservation.aligned_offset;
  const auto table_path = root
    / lc::ContentRelPath(imp::internal::loose_layout::kBuffersTable,
      oxygen::base::ComputeSha256(std::as_bytes(std::span(rows))));
  storage.FailPublishing(table_path);

  // Act
  auto end = imp::ResourceTableRegistry::SessionEnd {};
  co::Run(loop, [&] -> co::Co<> { end = co_await tables.EndSession(session); });

  // Assert
  EXPECT_FALSE(end.ok);
  EXPECT_TRUE(end.written.empty());
  EXPECT_FALSE(oxygen::cooker::test::HasCookedFile(
    root, imp::internal::loose_layout::kBuffersTable, storage));
}

} // namespace
