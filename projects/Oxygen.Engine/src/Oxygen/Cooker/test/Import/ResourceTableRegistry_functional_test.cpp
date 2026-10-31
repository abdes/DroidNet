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
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
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

class HeldCompletionWriter final : public imp::IAsyncFileWriter {
public:
  explicit HeldCompletionWriter(imp::ImportEventLoop& loop)
    : writer_(imp::CreateAsyncFileWriter(loop))
  {
  }
  ~HeldCompletionWriter() override = default;
  OXYGEN_MAKE_NON_COPYABLE(HeldCompletionWriter)
  OXYGEN_MAKE_NON_MOVABLE(HeldCompletionWriter)

  co::Event callback_held;
  co::Event table_write_held;
  auto HoldTableWrites() -> void { hold_tables_ = true; }
  auto ReleaseTableWrites() -> void
  {
    hold_tables_ = false;
    table_released_.Trigger();
  }
  auto Release() -> void
  {
    hold_ = false;
    if (!pending_) {
      throw std::logic_error("No held write callback");
    }
    auto pending = std::move(*pending_);
    pending_.reset();
    pending.callback(pending.error, pending.bytes);
    released_.Trigger();
  }
  auto Write(const std::filesystem::path& path,
    std::span<const std::byte> bytes, imp::WriteOptions options)
    -> co::Co<oxygen::Result<uint64_t, imp::FileErrorInfo>> override
  {
    if (hold_tables_ && path.extension() == ".table") {
      table_write_held.Trigger();
      co_await table_released_;
    }
    co_return co_await writer_->Write(path, bytes, options);
  }
  auto WriteAt(const std::filesystem::path& path, uint64_t offset,
    std::span<const std::byte> bytes, imp::WriteOptions options)
    -> co::Co<oxygen::Result<uint64_t, imp::FileErrorInfo>> override
  {
    return writer_->WriteAt(path, offset, bytes, options);
  }
  auto WriteAsync(const std::filesystem::path& path,
    std::span<const std::byte> bytes, imp::WriteOptions options,
    imp::WriteCompletionCallback callback) -> void override
  {
    writer_->WriteAsync(path, bytes, options, Gate(std::move(callback)));
  }
  auto WriteAtAsync(const std::filesystem::path& path, uint64_t offset,
    std::span<const std::byte> bytes, imp::WriteOptions options,
    imp::WriteCompletionCallback callback) -> void override
  {
    writer_->WriteAtAsync(
      path, offset, bytes, options, Gate(std::move(callback)));
  }
  auto Flush() -> co::Co<oxygen::Result<void, imp::FileErrorInfo>> override
  {
    const auto first = co_await writer_->Flush();
    if (pending_) {
      co_await released_;
    }
    const auto rest = co_await writer_->Flush();
    co_return first ? rest : first;
  }
  auto CancelAll() -> void override { writer_->CancelAll(); }
  [[nodiscard]] auto PendingCount() const -> size_t override
  {
    return writer_->PendingCount() + (pending_ ? 1U : 0U);
  }

private:
  struct Pending final {
    imp::WriteCompletionCallback callback;
    imp::FileErrorInfo error;
    uint64_t bytes = 0;
  };
  auto Gate(imp::WriteCompletionCallback callback)
    -> imp::WriteCompletionCallback
  {
    return [this, callback = std::move(callback)](
             const imp::FileErrorInfo& error, uint64_t bytes) -> void {
      if (hold_) {
        CHECK_F(!pending_);
        pending_.emplace(
          Pending { .callback = callback, .error = error, .bytes = bytes });
        callback_held.Trigger();
      } else {
        callback(error, bytes);
      }
    };
  }
  std::unique_ptr<imp::IAsyncFileWriter> writer_;
  std::optional<Pending> pending_;
  co::Event released_;
  co::Event table_released_;
  bool hold_tables_ = false;
  bool hold_ = true;
};

NOLINT_TEST(ResourceTableRegistryTest, TableAdmissionWaitsForCompletedOverwrite)
{
  const oxygen::testing::ScopedTempDir temp;
  imp::ImportEventLoop loop;
  HeldCompletionWriter writer(loop);
  writer.HoldTableWrites();
  imp::ResourceTableRegistry tables(writer,
    oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()));
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
      co_await writer.table_write_held;
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
      writer.ReleaseTableWrites();
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
  const auto writer = imp::CreateAsyncFileWriter(loop);
  auto storage = oxygen::cooker::testing::MemoryFileSystem {};
  const auto root = oxygen::content::testing::MemoryRoot() / "broken_table";
  // One byte: not a whole number of buffer rows.
  storage.AddFile(root / imp::internal::loose_layout::kBuffersTable,
    std::array { std::byte { 1U } });
  imp::ResourceTableRegistry tables(*writer, oxygen::make_observer(&storage));

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
  const auto writer = imp::CreateAsyncFileWriter(loop);
  auto& storage = oxygen::content::cooked::RealCookerStorage();
  imp::ResourceTableRegistry tables(*writer, oxygen::make_observer(&storage));
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
} // namespace
