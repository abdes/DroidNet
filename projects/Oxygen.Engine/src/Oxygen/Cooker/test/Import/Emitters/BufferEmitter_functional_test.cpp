//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/BufferEmitter.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::co::Co;
using oxygen::content::import::BufferEmitter;
using oxygen::content::import::BufferTableAggregator;
using oxygen::content::import::CookedBufferPayload;
using oxygen::content::import::DedupCollisionPolicy;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportEventLoop;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::ResourceTableRegistry;
using PakBufferResourceDesc = oxygen::data::pak::core::BufferResourceDesc;
namespace co = oxygen::co;
namespace loose_layout = oxygen::content::import::internal::loose_layout;

namespace {

using oxygen::cooker::test::ReadBytes;

//! Aligns a value up to the alignment boundary.
constexpr auto AlignUp(uint64_t value, uint64_t alignment) -> uint64_t
{
  if (alignment <= 1) {
    return value;
  }
  const auto remainder = value % alignment;
  return (remainder == 0) ? value : (value + (alignment - remainder));
}

//=== Test Helpers ===--------------------------------------------------------//

//! Create a test cooked buffer payload with specified size and usage.
/*!
 @param size_bytes Size of the buffer data.
 @param usage_flags Buffer usage flags (0x01=vertex, 0x02=index, etc.).
 @param alignment Required alignment for the buffer.
 @param element_stride Stride of each element (0 for raw/index buffers).
 @param fill_byte Byte value to fill the buffer with for verification.
*/
auto MakeTestBuffer(size_t size_bytes, uint32_t usage_flags = 0x01,
  uint64_t alignment = 16, uint32_t element_stride = 0,
  std::byte fill_byte = std::byte { 0xAB }) -> CookedBufferPayload
{
  CookedBufferPayload payload;
  payload.alignment = alignment;
  payload.usage_flags = usage_flags;
  payload.element_stride = element_stride;
  payload.element_format = 0;
  // Provide a deterministic non-zero hash for tests that expect distinct
  // buffers/indices without requiring SHA-256 hashing.
  const auto fill_u8 = static_cast<uint8_t>(fill_byte);
  payload.content_hash = (static_cast<uint64_t>(fill_u8) << 56U)
    ^ (alignment << 24U) ^ (static_cast<uint64_t>(usage_flags) << 12U)
    ^ (static_cast<uint64_t>(element_stride) << 4U)
    ^ static_cast<uint64_t>(size_bytes);

  // Fill payload with recognizable pattern
  payload.data.resize(size_bytes);
  for (size_t i = 0; i < size_bytes; ++i) {
    // Mix fill_byte with position for unique content
    payload.data.at(i)
      = static_cast<std::byte>(static_cast<unsigned>(fill_u8) ^ (i & 0xFFU));
  }

  return payload;
}

//! Parse buffer table from binary data.
/*!
 Deserializes the packed BufferResourceDesc entries from the table file.
 Each entry uses the packed BufferResourceDesc size from the current PAK format.
*/
auto ParseBufferTable(const std::vector<std::byte>& data)
  -> std::vector<PakBufferResourceDesc>
{
  static_assert(sizeof(PakBufferResourceDesc) == 29,
    "BufferResourceDesc must be 29 bytes per PAK format");

  const auto count = data.size() / sizeof(PakBufferResourceDesc);
  std::vector<PakBufferResourceDesc> table(count);
  std::memcpy(table.data(), data.data(), count * sizeof(PakBufferResourceDesc));
  return table;
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for BufferEmitter tests: the emitter and its tables publish to
//! a file system held in memory, through an inline driver.
class BufferEmitterTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    driver_ = std::make_unique<oxygen::serio::IoDriver>(
      oxygen::serio::IoDriver::CreateInline().value());
    table_registry_ = std::make_unique<ResourceTableRegistry>(
      *driver_, oxygen::make_observer(&files_));
  }

  auto TearDown() -> void override
  {
    emitter_.reset();
    table_registry_.reset();
    driver_.reset();
    loop_.reset();
  }

  //! The cooked root the emitter writes under.
  [[nodiscard]] static auto Root() -> std::filesystem::path
  {
    return "/cooked";
  }

  auto BufferAggregator() -> BufferTableAggregator&
  {
    return table_registry_->BufferAggregator(Root());
  }

  auto FinalizeTables() -> bool
  {
    bool ok = false;
    co::Run(*loop_, [&] -> Co<> {
      ok = co_await table_registry_->FinalizeAll();
      co_return;
    });
    return ok;
  }

  //! Whether the file `path` was written.
  [[nodiscard]] auto Exists(const std::filesystem::path& path) const -> bool
  {
    return files_.KindOf(path) == oxygen::content::cooked::PathKind::kFile;
  }

  //! The size of the file `path`.
  [[nodiscard]] auto SizeOf(const std::filesystem::path& path) const -> uint64_t
  {
    return files_.Bytes(path).size();
  }

  //! The bytes of the file `path`.
  [[nodiscard]] auto ReadBytes(const std::filesystem::path& path) const
    -> std::vector<std::byte>
  {
    const auto bytes = files_.Bytes(path);
    return { bytes.begin(), bytes.end() };
  }

  //! Runs `body` in a coroutine whose nursery receives the writes of the
  //! emitter it makes with `emitter_.emplace`; the nursery joins when `body`
  //! ends. The emitter stays for inspection afterwards.
  template <typename Body> auto RunInNursery(Body body) -> void
  {
    co::Run(*loop_, [&] -> Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        co_await body(writes);
        co_return co::kJoin;
      };
    });
  }

  oxygen::cooker::testing::MemoryFileSystem files_;
  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<oxygen::serio::IoDriver> driver_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  std::optional<BufferEmitter> emitter_;
};

//! Fixture for the one test that publishes real files under a temporary
//! directory.
class BufferEmitterRealFileTest : public oxygen::testing::TempDirTest {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    table_registry_ = std::make_unique<ResourceTableRegistry>(loop_->Driver(),
      oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()));
  }

  auto TearDown() -> void override
  {
    emitter_.reset();
    table_registry_.reset();
    loop_.reset();
  }

  auto BufferAggregator() -> BufferTableAggregator&
  {
    return table_registry_->BufferAggregator(TempDir());
  }

  //! Runs `body` in a coroutine whose nursery receives the writes of the
  //! emitter it makes with `emitter_.emplace`; the nursery joins when `body`
  //! ends. The emitter stays for inspection afterwards.
  template <typename Body> auto RunInNursery(Body body) -> void
  {
    co::Run(*loop_, [&] -> Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        co_await body(writes);
        co_return co::kJoin;
      };
    });
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  std::optional<BufferEmitter> emitter_;
};

//=== Basic Emission Tests
//===-------------------------------------------------//

NOLINT_TEST_F(BufferEmitterTest, EmitSingleBufferReturnsIndexOne)
{
  auto payload = MakeTestBuffer(1024, 0x01, 16, 32);

  uint32_t index = 0;
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    index = emitter.Emit(std::move(payload), "buf0");

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_EQ(index, 1);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1);
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(BufferEmitterTest, EmitMultipleBuffersReturnsSequentialIndices)
{

  std::vector<uint32_t> indices;
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    for (int i = 0; i < 5; ++i) {
      auto payload = MakeTestBuffer(512 + (i * 100), 0x01, 16, 32);
      indices.push_back(
        emitter.Emit(std::move(payload), "buf" + std::to_string(i)));
    }

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_EQ(indices.size(), 5);
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(indices.at(i), i + 1);
  }
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 5);
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(BufferEmitterTest, EmitDuplicateBufferReturnsSameIndex)
{

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xAB });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xAB });

    // Hashing is optional. Provide a non-zero stored hash to enable dedupe.
    buf0.content_hash = 0x1111222233334444ULL;
    buf1.content_hash = 0x1111222233334444ULL;

    idx0 = emitter.Emit(std::move(buf0), "dupe");
    idx1 = emitter.Emit(std::move(buf1), "dupe");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  EXPECT_EQ(idx0, 1);
  EXPECT_EQ(idx1, 1);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1);

  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));
  ASSERT_EQ(table.size(), 2);
}

//! With no content hash, payload fingerprint still dedups identical buffers.
NOLINT_TEST_F(BufferEmitterTest, DedupNoHashIdenticalPayloadAcrossSalts)
{

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xCD });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xCD });
    buf0.content_hash = 0;
    buf1.content_hash = 0;

    idx0 = emitter.Emit(std::move(buf0), "salt_a");
    idx1 = emitter.Emit(std::move(buf1), "salt_b");

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  EXPECT_EQ(idx0, 1U);
  EXPECT_EQ(idx1, 1U);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1U);

  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));
  EXPECT_EQ(table.size(), 2U); // includes sentinel
}

//! Collision policy keep-first preserves first mapping and warns.
NOLINT_TEST_F(
  BufferEmitterTest, CollisionPolicyWarnKeepFirstExpectedToKeepIndex)
{
  std::vector<ImportDiagnostic> diagnostics;
  BufferEmitter::Config config {};
  config.collision_policy = DedupCollisionPolicy::kWarnKeepFirst;
  config.on_dedup_diagnostic = [&](ImportDiagnostic diagnostic) -> void {
    diagnostics.push_back(std::move(diagnostic));
  };

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter
      = emitter_.emplace(writes, BufferAggregator(), Root(), std::move(config));
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xEF });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xE0 });
    buf0.content_hash = 0;
    buf1.content_hash = 0;

    idx0 = emitter.Emit(std::move(buf0), "same_salt");
    idx1 = emitter.Emit(std::move(buf1), "same_salt");

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  EXPECT_EQ(idx0, 1U);
  EXPECT_EQ(idx1, 1U);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1U);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.dedup_collision.buffer");
  EXPECT_EQ(diagnostics.front().severity, ImportSeverity::kWarning);

  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));
  EXPECT_EQ(table.size(), 2U); // sentinel + one emitted entry
}

//! Collision policy replace remaps key to newest payload.
NOLINT_TEST_F(BufferEmitterTest, CollisionPolicyWarnReplaceExpectedToEmitNew)
{
  std::vector<ImportDiagnostic> diagnostics;
  BufferEmitter::Config config {};
  config.collision_policy = DedupCollisionPolicy::kWarnReplace;
  config.on_dedup_diagnostic = [&](ImportDiagnostic diagnostic) -> void {
    diagnostics.push_back(std::move(diagnostic));
  };

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter
      = emitter_.emplace(writes, BufferAggregator(), Root(), std::move(config));
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x11 });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x22 });
    buf0.content_hash = 0;
    buf1.content_hash = 0;
    idx0 = emitter.Emit(std::move(buf0), "same_salt");
    idx1 = emitter.Emit(std::move(buf1), "same_salt");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  EXPECT_EQ(idx0, 1U);
  EXPECT_EQ(idx1, 2U);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 2U);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.dedup_collision.buffer");
  EXPECT_EQ(diagnostics.front().severity, ImportSeverity::kWarning);
}

//! Collision policy error rejects ambiguous key remap.
NOLINT_TEST_F(BufferEmitterTest, CollisionPolicyErrorExpectedToThrow)
{
  BufferEmitter::Config config {};
  config.collision_policy = DedupCollisionPolicy::kError;

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter
      = emitter_.emplace(writes, BufferAggregator(), Root(), std::move(config));
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x44 });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x55 });
    buf0.content_hash = 0;
    buf1.content_hash = 0;

    (void)emitter.Emit(std::move(buf0), "same_salt");
    EXPECT_THROW(
      (void)emitter.Emit(std::move(buf1), "same_salt"), std::runtime_error);
    co_return;
  });
}

//! Hashed buffers must bypass non-hash collision policy gating.
NOLINT_TEST_F(
  BufferEmitterTest, HashedSameSaltDifferentContentExpectedToEmitNewIndex)
{
  std::vector<ImportDiagnostic> diagnostics;
  BufferEmitter::Config config {};
  config.collision_policy = DedupCollisionPolicy::kWarnKeepFirst;
  config.on_dedup_diagnostic = [&](ImportDiagnostic diagnostic) -> void {
    diagnostics.push_back(std::move(diagnostic));
  };

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter
      = emitter_.emplace(writes, BufferAggregator(), Root(), std::move(config));
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x31 });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x32 });
    buf0.content_hash = 0x1111222233334444ULL;
    buf1.content_hash = 0xAAAABBBBCCCCDDDDULL;

    idx0 = emitter.Emit(std::move(buf0), "same_salt");
    idx1 = emitter.Emit(std::move(buf1), "same_salt");

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  EXPECT_EQ(idx0, 1U);
  EXPECT_EQ(idx1, 2U);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 2U);
  EXPECT_TRUE(diagnostics.empty());
}

//! Hashed buffers with identical content hash still dedup.
NOLINT_TEST_F(BufferEmitterTest, HashedSameSaltIdenticalContentExpectedToDedup)
{
  std::vector<ImportDiagnostic> diagnostics;
  BufferEmitter::Config config {};
  config.collision_policy = DedupCollisionPolicy::kWarnReplace;
  config.on_dedup_diagnostic = [&](ImportDiagnostic diagnostic) -> void {
    diagnostics.push_back(std::move(diagnostic));
  };

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter
      = emitter_.emplace(writes, BufferAggregator(), Root(), std::move(config));
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x7A });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0x7A });
    buf0.content_hash = 0xDEADBEEFCAFEBABEULL;
    buf1.content_hash = 0xDEADBEEFCAFEBABEULL;

    idx0 = emitter.Emit(std::move(buf0), "same_salt");
    idx1 = emitter.Emit(std::move(buf1), "same_salt");

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  EXPECT_EQ(idx0, 1U);
  EXPECT_EQ(idx1, 1U);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1U);
  EXPECT_TRUE(diagnostics.empty());
}

//! Emitter count exposes unique entries, not request total.
NOLINT_TEST_F(BufferEmitterTest, CountReflectsUniqueEntriesNotEmitRequests)
{
  constexpr int kEmitRequests = 5;

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    for (int i = 0; i < kEmitRequests; ++i) {
      auto buf = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xA5 });
      buf.content_hash = 0;
      (void)emitter.Emit(std::move(buf), "same_salt");
    }

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1U);

  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));
  EXPECT_EQ(table.size(), 2U);
}

NOLINT_TEST_F(BufferEmitterTest, EmitReturnsImmediatelyBeforeIOCompletes)
{
  auto payload = MakeTestBuffer(size_t { 4 } * 1024U, 0x01, 16, 32);

  uint32_t index = 0;
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    index = emitter.Emit(std::move(payload), "buf0");

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_EQ(index, 1); // Index assigned immediately (0 is sentinel)
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(BufferEmitterTest, EmitAfterFinalizeThrows)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW((void)emitter.Emit(MakeTestBuffer(256, 0x01, 16, 32), "buf0"),
      std::runtime_error);
  });
}

//=== PAK Format Compliance Tests ===-----------------------------------------//

NOLINT_TEST_F(BufferEmitterTest, FinalizeTableFileHasCorrectPackedSize)
{
  constexpr int kBufferCount = 3;

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    for (int i = 0; i < kBufferCount; ++i) {
      auto idx = emitter.Emit(
        MakeTestBuffer(256, 0x01, 16, 32, static_cast<std::byte>(0xA0 + i)),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, static_cast<uint32_t>(i + 1));
    }
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Table file size = count * sizeof(BufferResourceDesc)
  const auto table_path = Root() / loose_layout::kBuffersTable;
  EXPECT_TRUE(Exists(table_path));

  const auto table_size = SizeOf(table_path);
  EXPECT_EQ(table_size, (kBufferCount + 1) * sizeof(PakBufferResourceDesc));
  EXPECT_EQ(table_size, (kBufferCount + 1) * 29); // includes sentinel
}

NOLINT_TEST_F(BufferEmitterTest, FinalizeTableEntriesHaveCorrectAlignedOffsets)
{
  // Create buffers with different alignments

  // Buffer 0: 100 bytes with 16-byte alignment -> offset 0
  // Buffer 1: 200 bytes with 16-byte alignment -> offset = AlignUp(100, 16) =
  // 112 Buffer 2: 150 bytes with 4-byte alignment -> offset = AlignUp(112+200,
  // 4) = 312
  constexpr size_t kSize0 = 100;
  constexpr size_t kSize1 = 200;
  constexpr size_t kSize2 = 150;
  constexpr uint64_t kAlign0 = 16;
  constexpr uint64_t kAlign1 = 16;
  constexpr uint64_t kAlign2 = 4;

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign0, 32), "buf0");
    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign1, 32), "buf1");
    auto idx2 = emitter.Emit(MakeTestBuffer(kSize2, 0x02, kAlign2, 0), "buf2");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);
    EXPECT_EQ(idx2, 3);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Parse table and verify offsets
  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));

  ASSERT_EQ(table.size(), 4);

  // Verify offsets follow alignment rules
  const auto expected_offset0 = 0ULL;
  const auto expected_offset1 = AlignUp(expected_offset0 + kSize0, kAlign1);
  const auto expected_offset2 = AlignUp(expected_offset1 + kSize1, kAlign2);

  EXPECT_EQ(table.at(1).data_offset, expected_offset0);
  EXPECT_EQ(table.at(1).size_bytes, kSize0);

  EXPECT_EQ(table.at(2).data_offset, expected_offset1);
  EXPECT_EQ(table.at(2).size_bytes, kSize1);

  EXPECT_EQ(table.at(3).data_offset, expected_offset2);
  EXPECT_EQ(table.at(3).size_bytes, kSize2);

  // Verify all offsets are properly aligned
  EXPECT_EQ(table.at(1).data_offset % kAlign0, 0);
  EXPECT_EQ(table.at(2).data_offset % kAlign1, 0);
  EXPECT_EQ(table.at(3).data_offset % kAlign2, 0);
}

NOLINT_TEST_F(BufferEmitterTest, FinalizeTableEntriesPreserveMetadata)
{

  CookedBufferPayload vertex_payload;
  vertex_payload.data.resize(512);
  vertex_payload.alignment = 16;
  vertex_payload.usage_flags = 0x01; // Vertex buffer
  vertex_payload.element_stride = 32;
  vertex_payload.element_format = 0;
  vertex_payload.content_hash = 0xDEADBEEFCAFEBABE;

  CookedBufferPayload index_payload;
  index_payload.data.resize(256);
  index_payload.alignment = 4;
  index_payload.usage_flags = 0x02; // Index buffer
  index_payload.element_stride = 0;
  index_payload.element_format = 0;
  index_payload.content_hash = 0x1234567890ABCDEF;

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx0 = emitter.Emit(std::move(vertex_payload), "vb");
    auto idx1 = emitter.Emit(std::move(index_payload), "ib");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Parse table and verify metadata
  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));

  ASSERT_EQ(table.size(), 3);

  // Vertex buffer entry
  EXPECT_EQ(table.at(1).size_bytes, 512);
  EXPECT_EQ(table.at(1).usage_flags, 0x01);
  EXPECT_EQ(table.at(1).element_stride, 32);
  EXPECT_EQ(table.at(1).element_format, 0);
  EXPECT_EQ(table.at(1).content_hash, 0xDEADBEEFCAFEBABE);

  // Index buffer entry
  EXPECT_EQ(table.at(2).size_bytes, 256);
  EXPECT_EQ(table.at(2).usage_flags, 0x02);
  EXPECT_EQ(table.at(2).element_stride, 0);
  EXPECT_EQ(table.at(2).element_format, 0);
  EXPECT_EQ(table.at(2).content_hash, 0x1234567890ABCDEF);
}

//! On the real file system: the buffers land in the data file at their
//! aligned offsets, with zero padding between them.
NOLINT_TEST_F(BufferEmitterRealFileTest, FinalizeDataFileContainsCorrectContent)
{

  // Create two buffers with distinct content
  CookedBufferPayload buf0;
  buf0.data.resize(100);
  std::ranges::fill(buf0.data, std::byte { 0xAA });
  buf0.alignment = 16;
  buf0.usage_flags = 0x01;

  CookedBufferPayload buf1;
  buf1.data.resize(200);
  std::ranges::fill(buf1.data, std::byte { 0xBB });
  buf1.alignment = 16;
  buf1.usage_flags = 0x01;

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), TempDir());
    auto idx0 = emitter.Emit(std::move(buf0), "buf0");
    auto idx1 = emitter.Emit(std::move(buf1), "buf1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // Read data file and verify content
  const auto data_path = TempDir() / loose_layout::kBuffersData;
  const auto data_file = ReadBytes(data_path);

  // Buffer 0 at offset 0
  ASSERT_GE(data_file.size(), 100);
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(data_file.at(i), std::byte { 0xAA })
      << "Buffer 0 content mismatch at byte " << i;
  }

  // Buffer 1 at aligned offset = AlignUp(100, 16) = 112
  constexpr auto kOffset1 = AlignUp(100, 16);
  ASSERT_GE(data_file.size(), kOffset1 + 200);
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(data_file.at(kOffset1 + i), std::byte { 0xBB })
      << "Buffer 1 content mismatch at byte " << i;
  }

  // Verify padding between buffers is zeros
  for (size_t i = 100; i < kOffset1; ++i) {
    EXPECT_EQ(data_file.at(i), std::byte { 0x00 })
      << "Padding should be zeros at byte " << i;
  }
}

NOLINT_TEST_F(BufferEmitterTest, FinalizeDataFileSizeIncludesPadding)
{

  constexpr size_t kSize0 = 100;
  constexpr size_t kSize1 = 200;
  constexpr uint64_t kAlign = 16;

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign, 32), "buf0");
    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign, 32), "buf1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // Data file size = padding + data
  const auto data_path = Root() / loose_layout::kBuffersData;
  const auto data_size = SizeOf(data_path);

  // Expected: offset0=0, size0=100, offset1=AlignUp(100,16)=112, size1=200
  // Total = 112 + 200 = 312
  const auto expected_size = AlignUp(kSize0, kAlign) + kSize1;
  EXPECT_EQ(data_size, expected_size);
}

//=== Finalization Tests
//===---------------------------------------------------//

NOLINT_TEST_F(BufferEmitterTest, FinalizeWaitsForPendingIO)
{
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx0 = emitter.Emit(MakeTestBuffer(2048, 0x01, 16, 32), "buf0");
    auto idx1 = emitter.Emit(MakeTestBuffer(1024, 0x02, 4, 0), "buf1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->ErrorCount(), 0);
}

NOLINT_TEST_F(BufferEmitterTest, FinalizeNoBuffersSucceedsWithoutWritingFiles)
{

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);

  const auto table_path = Root() / loose_layout::kBuffersTable;
  EXPECT_FALSE(Exists(table_path));
}

//=== State Query Tests
//===----------------------------------------------------//

NOLINT_TEST_F(BufferEmitterTest, DataFileSizeTracksAccumulatedSize)
{
  constexpr size_t kSize0 = 100;
  constexpr size_t kSize1 = 200;
  constexpr uint64_t kAlign = 16;

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    // Assert initial state
    EXPECT_EQ(emitter.DataFileSize(), 0);

    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign, 32), "buf0");
    EXPECT_EQ(idx0, 1);
    // First buffer: offset 0, size 100 -> file size 100
    EXPECT_EQ(emitter.DataFileSize(), kSize0);

    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign, 32), "buf1");
    EXPECT_EQ(idx1, 2);
    // Second buffer: offset = AlignUp(100, 16) = 112, size 200 -> file size 312
    EXPECT_EQ(emitter.DataFileSize(), AlignUp(kSize0, kAlign) + kSize1);

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);
}

NOLINT_TEST_F(BufferEmitterTest, CountTracksEmittedBuffers)
{
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());

    // Assert initial state
    EXPECT_EQ(emitter.Count(), 0);

    for (uint32_t i = 0; i < 10; ++i) {
      auto idx = emitter.Emit(
        MakeTestBuffer(64, 0x01, 16, 32, static_cast<std::byte>(0xC0 + i)),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, i + 1);
      EXPECT_EQ(emitter.Count(), i + 1);
    }

    success = (co_await emitter.Finalize()).has_value();
  });

  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 10);
  EXPECT_TRUE(success);
}

//=== Edge Cases ===----------------------------------------------------------//

NOLINT_TEST_F(BufferEmitterTest, EmitZeroAlignmentUsesDefaultAlignment)
{

  // Buffer with zero alignment should use default (16)
  auto payload = MakeTestBuffer(100, 0x01, 0, 32);

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx = emitter.Emit(std::move(payload), "buf0");
    EXPECT_EQ(idx, 1);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Table entry exists
  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));

  ASSERT_EQ(table.size(), 2);
  EXPECT_EQ(table.at(1).size_bytes, 100);
}

NOLINT_TEST_F(BufferEmitterTest, EmitLargeBufferSucceedsWithCorrectSize)
{

  // 1MB buffer
  constexpr size_t kLargeSize = size_t { 1024 } * 1024U;
  auto payload = MakeTestBuffer(kLargeSize, 0x01, 16, 32);

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    auto idx = emitter.Emit(std::move(payload), "buf0");
    EXPECT_EQ(idx, 1);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Data file has correct size
  const auto data_path = Root() / loose_layout::kBuffersData;
  EXPECT_EQ(SizeOf(data_path), kLargeSize);

  // Table entry has correct size
  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));

  ASSERT_EQ(table.size(), 2);
  EXPECT_EQ(table.at(1).size_bytes, kLargeSize);
}

NOLINT_TEST_F(BufferEmitterTest, EmitManySmallBuffersAllAlignedCorrectly)
{

  constexpr int kBufferCount = 50;
  constexpr size_t kBufferSize = 17; // Intentionally not aligned
  constexpr uint64_t kAlignment = 16;

  bool tables_ok = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(writes, BufferAggregator(), Root());
    for (int i = 0; i < kBufferCount; ++i) {
      auto idx = emitter.Emit(
        MakeTestBuffer(kBufferSize, 0x01, kAlignment, 32,
          static_cast<std::byte>(0x10U + (static_cast<unsigned>(i) & 0x7FU))),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, static_cast<uint32_t>(i + 1));
    }
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // All table entries have aligned offsets
  const auto table_path = Root() / loose_layout::kBuffersTable;
  const auto table = ParseBufferTable(ReadBytes(table_path));

  ASSERT_EQ(table.size(), kBufferCount + 1);

  uint64_t expected_offset = 0;
  for (int i = 0; i < kBufferCount; ++i) {
    EXPECT_EQ(table.at(i + 1).data_offset, expected_offset)
      << "Buffer " << i << " has wrong offset";
    EXPECT_EQ(table.at(i + 1).data_offset % kAlignment, 0)
      << "Buffer " << i << " offset not aligned";
    EXPECT_EQ(table.at(i + 1).size_bytes, kBufferSize)
      << "Buffer " << i << " has wrong size";

    // Calculate next expected offset
    expected_offset = AlignUp(expected_offset + kBufferSize, kAlignment);
  }
}

} // namespace
