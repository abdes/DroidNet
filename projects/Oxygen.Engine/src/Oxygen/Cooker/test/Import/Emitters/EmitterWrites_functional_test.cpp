//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooked/CookerStorage.cpp, testing/MemoryFileSystem.h,
// Import/Internal/Emitters/AssetEmitter.cpp,
// Import/Internal/Emitters/BufferEmitter.cpp,
// Import/Internal/Emitters/WriteGroup.h

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/HoldingStorage.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::content::import::AssetEmitter;
using oxygen::content::import::BufferEmitter;
using oxygen::content::import::CookedBufferPayload;
using oxygen::content::import::CookedPhysicsResourcePayload;
using oxygen::content::import::ImportEventLoop;
using oxygen::content::import::PhysicsResourceEmitter;
using oxygen::content::import::ResourceDescriptorEmitter;
using oxygen::content::import::ResourceTableRegistry;
using oxygen::content::import::WriteFailure;
namespace co = oxygen::co;
namespace cooked = oxygen::content::cooked;
namespace fixtures = oxygen::cooker::testing;
namespace internal = oxygen::content::import::internal;
namespace serio = oxygen::serio;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;

namespace {

//! Bytes of `text`.
auto BytesOf(const std::string_view text) -> std::vector<std::byte>
{
  const auto bytes = std::as_bytes(std::span(text));
  return { bytes.begin(), bytes.end() };
}

//! Storage contract and emitter write errors, on files and in memory.
class EmitterWritesTest : public oxygen::testing::TempDirTest {
protected:
  auto SetUp() -> void override { loop_ = std::make_unique<ImportEventLoop>(); }

  auto TearDown() -> void override { loop_.reset(); }

  //! Publishes `bytes` as `path` through `storage` on the loop.
  auto Publish(cooked::ImportStorage& storage,
    const std::filesystem::path& path, const std::span<const std::byte> bytes)
    -> oxygen::Result<void>
  {
    return co::Run(*loop_, storage.PublishAsync(loop_->Driver(), path, bytes));
  }

  std::unique_ptr<ImportEventLoop> loop_;
};

//=== PublishAsync ===--------------------------------------------------------//

//! A publication replaces the file whole and leaves no sibling behind.
NOLINT_TEST_F(EmitterWritesTest, PublishAsyncReplacesTheFileAndLeavesNoTemp)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "descriptor.bin";
  oxygen::cooker::test::WriteText(path, "an older and longer content");
  const auto bytes = BytesOf("new");

  const auto published = Publish(storage, path, bytes);

  ASSERT_TRUE(published);
  EXPECT_EQ(oxygen::cooker::test::ReadBytes(path), bytes);
  for (const auto& entry : std::filesystem::directory_iterator(TempDir())) {
    EXPECT_FALSE(entry.path().filename().string().starts_with(".oxygen-"))
      << "left behind: " << entry.path();
  }
}

//! A reader that opened the file before the publication keeps the bytes it
//! opened: the file is replaced, not rewritten in place.
NOLINT_TEST_F(EmitterWritesTest, PublishAsyncLeavesAnOpenReaderTheOldBytes)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "descriptor.bin";
  const auto old_bytes = BytesOf("old bytes");
  oxygen::cooker::test::WriteBytes(path, old_bytes);
  const auto reader = serio::OpenPublishedFile(path);
  ASSERT_TRUE(reader);

  const auto published = Publish(storage, path, BytesOf("new"));

  ASSERT_TRUE(published);
  const auto seen = serio::ReadAll(*reader);
  ASSERT_TRUE(seen);
  EXPECT_EQ(std::vector<std::byte>(seen->Bytes().begin(), seen->Bytes().end()),
    old_bytes);
  EXPECT_EQ(oxygen::cooker::test::ReadBytes(path), BytesOf("new"));
}

//! The parent folders of the destination are created.
NOLINT_TEST_F(EmitterWritesTest, PublishAsyncCreatesParentFolders)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "a" / "b" / "descriptor.bin";

  const auto published = Publish(storage, path, BytesOf("bytes"));

  ASSERT_TRUE(published);
  EXPECT_EQ(oxygen::cooker::test::ReadBytes(path), BytesOf("bytes"));
}

//! The in-memory storage keeps what is published, and refuses what it was told
//! to refuse.
NOLINT_TEST_F(EmitterWritesTest, PublishAsyncInMemoryStoresOrRefuses)
{
  auto storage = fixtures::MemoryFileSystem {};
  const auto root = std::filesystem::path("/memory/publish");

  const auto stored = Publish(storage, root / "a.bin", BytesOf("abc"));
  storage.FailPublishing(root / "b.bin");
  const auto refused = Publish(storage, root / "b.bin", BytesOf("abc"));

  ASSERT_TRUE(stored);
  EXPECT_EQ(storage.Bytes(root / "a.bin").size(), 3U);
  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error(), std::errc::permission_denied);
  EXPECT_EQ(storage.KindOf(root / "b.bin"), cooked::PathKind::kNone);
}

//=== OpenData ===------------------------------------------------------------//

//! The data file is created when missing and never truncated; ranges written
//! through two devices of one file both land.
NOLINT_TEST_F(EmitterWritesTest, OpenDataKeepsExistingBytesAndBothRangesLand)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "data" / "buffers.data";
  std::filesystem::create_directories(path.parent_path());
  oxygen::cooker::test::WriteBytes(path, BytesOf("0123"));
  auto first = storage.OpenData(loop_->Driver(), path);
  auto second = storage.OpenData(loop_->Driver(), path);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  const auto high = BytesOf("hi");
  const auto low = BytesOf("lo");

  co::Run(*loop_, [&] -> co::Co<> {
    EXPECT_TRUE(co_await second->AsyncWriteAt(serio::ByteOffset { 6 }, high));
    EXPECT_TRUE(co_await first->AsyncWriteAt(serio::ByteOffset { 4 }, low));
  });

  EXPECT_EQ(oxygen::cooker::test::ReadBytes(path), BytesOf("0123lohi"));
}

//! A missing data file is created, with its folders.
NOLINT_TEST_F(EmitterWritesTest, OpenDataCreatesTheFileAndItsFolders)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "new" / "folder" / "physics.data";

  auto device = storage.OpenData(loop_->Driver(), path);

  ASSERT_TRUE(device);
  EXPECT_EQ(storage.KindOf(path), cooked::PathKind::kFile);
  EXPECT_EQ(storage.SizeOf(path), serio::ByteCount { 0 });
}

//! A data file the import holds open for its appends can still be read: the
//! storage maps it although a writer holds it.
NOLINT_TEST_F(EmitterWritesTest, OpenMapsADataFileOpenForWrites)
{
  auto& storage = cooked::RealCookerStorage();
  const auto path = TempDir() / "Resources" / "buffers.data";
  const auto bytes = std::vector<std::byte>(64, std::byte { 5 });
  auto device = storage.OpenData(loop_->Driver(), path);
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->WriteAt(serio::ByteOffset { 0 }, bytes));

  const auto mapped = storage.Open(path);

  ASSERT_TRUE(mapped) << mapped.error().message();
  const auto contiguous = mapped->Contiguous();
  ASSERT_HAS_VALUE(contiguous);
  EXPECT_TRUE(std::ranges::equal(*contiguous, bytes));
}

//! Cooked writes belong to the nursery of the session that started them:
//! cancelling one session's nursery cancels its writes and leaves another
//! session's writes complete.
NOLINT_TEST_F(EmitterWritesTest, WritesBelongToTheNurseryThatStartedThem)
{
  auto& storage = cooked::RealCookerStorage();
  const auto descriptor = oxygen::data::pak::core::BufferResourceDesc {};
  auto other_result = oxygen::Result<void, WriteFailure> {};
  // Outlives its nursery, which joins the writes it started.
  auto first = std::optional<ResourceDescriptorEmitter> {};

  co::Run(*loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(other)
    {
      auto second = ResourceDescriptorEmitter(
        storage, loop_->Driver(), other, TempDir() / "second");
      static_cast<void>(second.EmitBufferAtRelPath("second.obuf",
        oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));
      OXCO_WITH_NURSERY(cancelled)
      {
        first.emplace(storage, loop_->Driver(), cancelled, TempDir() / "first");
        static_cast<void>(first->EmitBufferAtRelPath("first.obuf",
          oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));
        // Before its write ran: the first session's write never completes.
        cancelled.Cancel();
        co_return co::kJoin;
      };
      other_result = co_await second.Finalize();
      co_return co::kJoin;
    };
  });

  EXPECT_TRUE(other_result);
  EXPECT_TRUE(std::filesystem::exists(TempDir() / "second" / "second.obuf"));
  EXPECT_FALSE(std::filesystem::exists(TempDir() / "first" / "first.obuf"));
}

//! The in-memory device writes the stored bytes of the file: readers of the
//! storage see them, and bytes written before stay.
NOLINT_TEST_F(EmitterWritesTest, OpenDataInMemoryWritesTheStoredFile)
{
  auto storage = fixtures::MemoryFileSystem {};
  const auto path = std::filesystem::path("/memory/data/textures.data");
  storage.AddFile(path, BytesOf("0123"));
  auto device = storage.OpenData(loop_->Driver(), path);
  ASSERT_TRUE(device);
  const auto tail = BytesOf("tail");

  co::Run(*loop_, [&] -> co::Co<> {
    EXPECT_TRUE(co_await device->AsyncWriteAt(serio::ByteOffset { 4 }, tail));
  });

  EXPECT_EQ(storage.SizeOf(path), serio::ByteCount { 8 });
  const auto stored = storage.Bytes(path);
  EXPECT_EQ(
    std::vector<std::byte>(stored.begin(), stored.end()), BytesOf("0123tail"));
}

//=== Emitters ===------------------------------------------------------------//

//! `Finalize` returns the first error a write hit, and every error is counted.
NOLINT_TEST_F(EmitterWritesTest, AssetEmitterFinalizeReturnsTheFirstWriteError)
{
  auto storage = fixtures::MemoryFileSystem {};
  const auto root = std::filesystem::path("/memory/asset_errors");
  storage.FailPublishing(root / "Materials/A.omat",
    std::make_error_code(std::errc::no_space_on_device));
  storage.FailPublishing(root / "Materials/B.omat");
  auto result = oxygen::Result<void, WriteFailure> {};
  size_t errors = 0;

  co::Run(*loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(writes)
    {
      AssetEmitter emitter(storage, loop_->Driver(), writes, root);
      emitter.Emit(AssetKey::FromVirtualPath("/.cooked/Materials/A.omat"),
        AssetType::kMaterial, "/.cooked/Materials/A.omat", "Materials/A.omat",
        fixtures::MaterialDescriptor("a").bytes, {});
      emitter.Emit(AssetKey::FromVirtualPath("/.cooked/Materials/B.omat"),
        AssetType::kMaterial, "/.cooked/Materials/B.omat", "Materials/B.omat",
        fixtures::MaterialDescriptor("b").bytes, {});
      result = co_await emitter.Finalize();
      errors = emitter.ErrorCount();
      co_return co::kJoin;
    };
  });

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().error, std::errc::no_space_on_device);
  // Both writes fail; whichever fails first is reported, with its file.
  EXPECT_EQ(result.error().path.extension(), ".omat");
  EXPECT_EQ(errors, 2U);
}

//! Emissions of one path while its write is in flight are coalesced: one write
//! at a time, and the latest bytes win.
NOLINT_TEST_F(
  EmitterWritesTest, AssetEmitterLatestBytesWinWhileAWriteIsInFlight)
{
  auto memory = fixtures::MemoryFileSystem {};
  fixtures::HoldingStorage storage(memory);
  const auto root = std::filesystem::path("/memory/asset_coalesce");
  const auto key = AssetKey::FromVirtualPath("/.cooked/Materials/Wood.omat");
  auto result = oxygen::Result<void, WriteFailure> {};

  co::Run(*loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(writes)
    {
      AssetEmitter emitter(storage, loop_->Driver(), writes, root);
      for (const std::string_view name : { "v1", "v2", "v3" }) {
        emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
          "Materials/Wood.omat", fixtures::MaterialDescriptor(name).bytes, {});
        if (name[1] == '1') {
          // The first write stops in flight; the others queue behind it.
          co_await storage.publication_held;
        }
      }
      EXPECT_EQ(storage.InFlight(), 1U);
      storage.Release();
      result = co_await emitter.Finalize();
      co_return co::kJoin;
    };
  });

  ASSERT_TRUE(result);
  const auto stored = memory.Bytes(root / "Materials/Wood.omat");
  const auto latest = fixtures::MaterialDescriptor("v3").bytes;
  EXPECT_EQ(std::vector<std::byte>(stored.begin(), stored.end()), latest);
}

//! A data write that fails is the error `Finalize` returns; the index the
//! emitter handed out stays valid.
NOLINT_TEST_F(EmitterWritesTest, BufferEmitterFinalizeReturnsTheDataWriteError)
{
  auto storage = fixtures::MemoryFileSystem {};
  const auto root = std::filesystem::path("/memory/buffer_errors");
  ResourceTableRegistry tables(
    loop_->Driver(), oxygen::make_observer<cooked::ImportStorage>(&storage));
  storage.FailDataWrites(root / internal::loose_layout::kBuffersData);
  auto result = oxygen::Result<void, WriteFailure> {};
  uint32_t index = 0;

  co::Run(*loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(writes)
    {
      BufferEmitter emitter(writes, tables.BufferAggregator(root), root);
      CookedBufferPayload payload;
      payload.alignment = 16;
      payload.usage_flags = 1;
      payload.element_stride = 16;
      payload.data.resize(64, std::byte { 7 });
      payload.content_hash = oxygen::base::ComputeSha256(payload.data);
      index = emitter.Emit(std::move(payload));
      result = co_await emitter.Finalize();
      co_return co::kJoin;
    };
  });

  EXPECT_EQ(index, 1U);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().error, serio::Error::kNoSpace);
  EXPECT_EQ(result.error().path.filename(), "buffers.data");
}

//! The physics resource emitter refuses a payload without its SHA-256.
NOLINT_TEST_F(EmitterWritesTest, PhysicsResourceEmitterRefusesAZeroHash)
{
  auto storage = fixtures::MemoryFileSystem {};
  const auto root = std::filesystem::path("/memory/physics_zero_hash");
  ResourceTableRegistry tables(
    loop_->Driver(), oxygen::make_observer<cooked::ImportStorage>(&storage));
  auto refused = false;

  co::Run(*loop_, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(writes)
    {
      PhysicsResourceEmitter emitter(
        writes, tables.PhysicsAggregator(root), root);
      CookedPhysicsResourcePayload payload;
      payload.data.resize(64, std::byte { 7 });
      try {
        static_cast<void>(emitter.Emit(std::move(payload)));
      } catch (const std::invalid_argument&) {
        refused = true;
      }
      co_return co::kJoin;
    };
  });

  EXPECT_TRUE(refused);
}

} // namespace
