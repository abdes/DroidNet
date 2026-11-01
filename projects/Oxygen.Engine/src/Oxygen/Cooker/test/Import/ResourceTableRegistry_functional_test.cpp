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
#include <utility>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/test/Support/HoldingStorage.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {
namespace co = oxygen::co;
namespace imp = oxygen::content::import;

using oxygen::cooker::testing::HoldingStorage;

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
  const auto inserted = tables.BufferAggregator(root).AcquireOrInsert("first",
    [] -> std::pair<oxygen::data::pak::core::BufferResourceDesc,
         oxygen::content::import::WriteReservation> {
      return std::pair { oxygen::data::pak::core::BufferResourceDesc {},
        imp::WriteReservation {} };
    });
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
        auto second = tables.BeginSession(root);
        second_index
          = tables.BufferAggregator(root)
              .AcquireOrInsert("second",
                [] -> std::pair<oxygen::data::pak::core::BufferResourceDesc,
                     oxygen::content::import::WriteReservation> {
                  return std::pair {
                    oxygen::data::pak::core::BufferResourceDesc {},
                    imp::WriteReservation {}
                  };
                })
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

//! The last session of a cohort writes the tables and reports the digest of
//! each as it wrote it; the sessions before it write nothing.
//! A table the cooked root holds but that cannot be read as rows fails the
//! import: starting it empty would publish a table that drops the root's
//! existing resources.
NOLINT_TEST(ResourceTableRegistryTest, UnreadableExistingTableFailsTheImport)
{
  // Arrange
  imp::ImportEventLoop loop;
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  const auto root = oxygen::content::testing::MemoryRoot() / "broken_table";
  // One byte: not a whole number of buffer rows.
  storage.AddFile(root / imp::internal::loose_layout::kBuffersTable,
    std::array { std::byte { 1U } });
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));

  // Act + Assert
  EXPECT_THROW(
    static_cast<void>(tables.BufferAggregator(root)), imp::ResourceTableError);
  EXPECT_EQ(storage.SizeOf(root / imp::internal::loose_layout::kBuffersTable),
    oxygen::serio::ByteCount { 1U });
}

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
  static_cast<void>(tables.BufferAggregator(root).AcquireOrInsert("only",
    [] -> std::pair<oxygen::data::pak::core::BufferResourceDesc,
         imp::WriteReservation> {
      return std::pair { oxygen::data::pak::core::BufferResourceDesc {},
        imp::WriteReservation {} };
    }));

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
  const auto bytes = oxygen::content::cooked::ReadWholeFile(
    storage, root / imp::internal::loose_layout::kBuffersTable);
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
  storage.AddFile(
    data_path, std::array { std::byte { 0xAA }, std::byte { 0xBB } });
  imp::ResourceTableRegistry tables(
    loop.Driver(), oxygen::make_observer(&storage));
  auto& aggregator = tables.BufferAggregator(root);
  const auto first
    = aggregator.ReserveDataRange({ .alignment = 1U, .payload_size = 3U });
  const auto second
    = aggregator.ReserveDataRange({ .alignment = 1U, .payload_size = 2U });
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
  static_cast<void>(tables.BufferAggregator(root).AcquireOrInsert("only",
    [] -> std::pair<oxygen::data::pak::core::BufferResourceDesc,
         imp::WriteReservation> {
      return std::pair { oxygen::data::pak::core::BufferResourceDesc {},
        imp::WriteReservation {} };
    }));
  // The empty table is created at the first entry; its publication fails.
  storage.FailPublishing(root / imp::internal::loose_layout::kBuffersTable);

  // Act
  auto end = imp::ResourceTableRegistry::SessionEnd {};
  co::Run(loop, [&] -> co::Co<> { end = co_await tables.EndSession(session); });

  // Assert
  EXPECT_FALSE(end.ok);
  EXPECT_TRUE(end.written.empty());
}

} // namespace
