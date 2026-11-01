//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/TextureEmitter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using PakTextureResourceDesc = oxygen::data::pak::core::TextureResourceDesc;
namespace import = oxygen::content::import;
namespace co = oxygen::co;
namespace loose_layout = import::internal::loose_layout;

namespace {

using oxygen::cooker::test::ReadBytes;

// Alignment used by TextureEmitter (matches kRowPitchAlignment)
constexpr uint64_t kTextureAlignment = 256;

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

//! Parse texture table from binary data.
auto ParseTextureTable(const std::vector<std::byte>& data)
  -> std::vector<PakTextureResourceDesc>
{
  const auto count = data.size() / sizeof(PakTextureResourceDesc);
  std::vector<PakTextureResourceDesc> table(count);
  const auto dest_span
    = std::span<PakTextureResourceDesc>(table.data(), table.size());
  auto dest_bytes = std::as_writable_bytes(dest_span);
  const auto bytes_to_copy = std::min(data.size(), dest_bytes.size());
  std::copy_n(data.begin(), bytes_to_copy, dest_bytes.begin());
  return table;
}

//=== Test Fixture ===--------------------------------------------------------//

//! The helpers every TextureEmitter fixture shares; `Base` supplies the test
//! (and, for real files, the temporary directory) and the derived fixture the
//! storage.
template <typename Base> class TextureEmitterFixture : public Base {
protected:
  // Local type aliases so member signatures can use unqualified names.
  using TextureEmitter = import::TextureEmitter;
  using TextureTableAggregator = import::TextureTableAggregator;
  using ImportEventLoop = import::ImportEventLoop;
  using ResourceTableRegistry = import::ResourceTableRegistry;

  struct PayloadProps {
    uint32_t width;
    uint32_t height;
    uint16_t mip_levels;
    size_t data_size;
  };

  auto SetUp() -> void override { loop_ = std::make_unique<ImportEventLoop>(); }

  auto TearDown() -> void override
  {
    table_registry_.reset();
    loop_.reset();
  }

  //! The cooked root the emitters write under.
  [[nodiscard]] virtual auto Root() const -> std::filesystem::path = 0;

  [[nodiscard]] auto MakeEmitterConfig() const -> TextureEmitter::Config
  {
    return TextureEmitter::Config {
      .cooked_root = Root(),
      .packing_policy_id = "d3d12",
      .data_alignment = kTextureAlignment,
    };
  }

  //! Create a test cooked texture payload with specified dimensions.
  static auto MakeTestPayload(const PayloadProps& payload_props,
    const bool with_content_hashing = false) -> import::CookedTexturePayload
  {
    import::CookedTexturePayload payload;
    payload.desc.width = payload_props.width;
    payload.desc.height = payload_props.height;
    payload.desc.mip_levels = payload_props.mip_levels;
    payload.desc.depth = 1;
    payload.desc.array_layers = 1;
    payload.desc.texture_type = oxygen::TextureType::kTexture2D;
    payload.desc.format = oxygen::Format::kBC7UNorm;

    // Default to no hashing so tests cover the fast identity path.
    payload.desc.content_hash = 0;

    // Fill payload with recognizable pattern
    payload.payload.resize(payload_props.data_size);
    for (size_t i = 0; i < payload_props.data_size; ++i) {
      constexpr size_t kByteMask = std::numeric_limits<uint8_t>::max();
      payload.payload.at(i) = static_cast<std::byte>(i & kByteMask);
    }

    if (with_content_hashing) {
      payload.desc.content_hash
        = import::util::ComputeContentHash(payload.payload);
      if (payload.desc.content_hash == 0) {
        payload.desc.content_hash = 1;
      }
    }

    return payload;
  }

  //! Create a test cooked texture payload with default properties.
  static auto MakeTestPayload() -> import::CookedTexturePayload
  {
    return MakeTestPayload({
      .width = kDefaultTextureWidth,
      .height = kDefaultTextureHeight,
      .mip_levels = kDefaultTextureMips,
      .data_size = kDefaultTextureDataSize,
    });
  }

  //! Runs `body(writes)` in a coroutine whose nursery receives the writes of
  //! the emitters it makes; the nursery joins when `body` ends.
  template <typename Body> auto RunInNursery(Body body) -> void
  {
    co::Run(*loop_, [&] -> co::Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        co_await body(writes);
        co_return co::kJoin;
      };
    });
  }

  [[nodiscard]] auto TextureAggregator() const -> TextureTableAggregator&
  {
    return table_registry_->TextureAggregator(Root());
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  static constexpr uint32_t kDefaultTextureWidth = 4;
  static constexpr uint32_t kDefaultTextureHeight = 4;
  static constexpr uint16_t kDefaultTextureMips = 1;
  static constexpr size_t kDefaultTextureDataSize = 128;

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

//! Test fixture for TextureEmitter tests: the emitters and their tables
//! publish to a file system held in memory, through an inline driver.
class TextureEmitterTest : public TextureEmitterFixture<::testing::Test> {
protected:
  using Fixture = TextureEmitterFixture<::testing::Test>;

  auto SetUp() -> void override
  {
    Fixture::SetUp();
    driver_ = std::make_unique<oxygen::serio::IoDriver>(
      oxygen::serio::IoDriver::CreateInline().value());
    table_registry_ = std::make_unique<ResourceTableRegistry>(
      *driver_, oxygen::make_observer(&files_));
  }

  auto TearDown() -> void override
  {
    Fixture::TearDown();
    driver_.reset();
  }

  [[nodiscard]] auto Root() const -> std::filesystem::path override
  {
    return "/cooked";
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

  oxygen::cooker::testing::MemoryFileSystem files_;
  std::unique_ptr<oxygen::serio::IoDriver> driver_;
};

//! Fixture for the one test that publishes real files under a temporary
//! directory.
class TextureEmitterRealFileTest
  : public TextureEmitterFixture<oxygen::testing::TempDirTest> {
protected:
  auto SetUp() -> void override
  {
    TextureEmitterFixture::SetUp();
    table_registry_ = std::make_unique<ResourceTableRegistry>(loop_->Driver(),
      oxygen::make_observer(&oxygen::content::cooked::RealCookerStorage()));
  }

  [[nodiscard]] auto Root() const -> std::filesystem::path override
  {
    return TempDir();
  }
};

//=== Basic Emission Tests ===------------------------------------------------//

NOLINT_TEST_F(
  TextureEmitterTest, ExplicitFallbackIsAvailableBeforeAnyUserTexture)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    EXPECT_FALSE(
      emitter.TryGetDescriptor(oxygen::data::pak::core::kFallbackResourceIndex)
        .has_value());
    emitter.EnsureFallbackTexture();
    EXPECT_TRUE(
      emitter.TryGetDescriptor(oxygen::data::pak::core::kFallbackResourceIndex)
        .has_value());
    const auto size = emitter.GetStats().data_file_size;
    EXPECT_GT(size, 0U);
    emitter.EnsureFallbackTexture();
    EXPECT_EQ(emitter.GetStats().data_file_size, size);
    EXPECT_TRUE(co_await emitter.Finalize());
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitSingleTextureAssignsFirstIndex)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();

    const auto index = emitter.Emit(std::move(payload), "test_texture");
    const auto success = co_await emitter.Finalize();

    EXPECT_EQ(index, 1);
    EXPECT_TRUE(success);
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitUniqueTexturesAssignsSequentialIndices)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    constexpr std::array<std::string_view, 5> kSalts {
      "t0",
      "t1",
      "t2",
      "t3",
      "t4",
    };
    std::vector<uint32_t> indices;
    indices.reserve(kSalts.size());
    for (size_t i = 0; i < kSalts.size(); ++i) {
      const auto salt = kSalts.at(i);
      auto payload = MakeTestPayload();
      if (!payload.payload.empty()) {
        payload.payload.at(0) = static_cast<std::byte>(i + 1);
      }
      indices.push_back(emitter.Emit(std::move(payload), salt));
    }

    const auto success = co_await emitter.Finalize();

    EXPECT_EQ(indices.size(), kSalts.size());
    for (size_t i = 0; i < kSalts.size(); ++i) {
      EXPECT_EQ(indices.at(i), i + 1);
    }
    EXPECT_TRUE(success);
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitStartsWriteAndReturnsBeforeFinalize)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();

    const uint32_t index = emitter.Emit(std::move(payload), "test_texture");
    const auto success = co_await emitter.Finalize();

    EXPECT_EQ(index, 1);
    EXPECT_TRUE(success);
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitAfterFinalizeThrows)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    NOLINT_EXPECT_THROW((void)emitter.Emit(MakeTestPayload(), "test_texture"),
      std::runtime_error);
  });
}

//=== Finalization Tests ===--------------------------------------------------//

NOLINT_TEST_F(TextureEmitterTest, FinalizeDrainsPendingWrites)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    (void)emitter.Emit(MakeTestPayload(), "t0");
    (void)emitter.Emit(MakeTestPayload(), "t1");

    const auto success = co_await emitter.Finalize();

    EXPECT_TRUE(success);
    EXPECT_EQ(emitter.GetStats().error_count, 0);
  });
}

NOLINT_TEST_F(TextureEmitterTest, FinalizeWritesTextureTableFile)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload0 = MakeTestPayload();
    auto payload1 = MakeTestPayload();
    if (!payload1.payload.empty()) {
      payload1.payload.at(0) ^= std::byte { 0xFF };
    }
    const auto idx0 = emitter.Emit(std::move(payload0), "t0");
    const auto idx1 = emitter.Emit(std::move(payload1), "t1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    EXPECT_TRUE(Exists(table_path));

    const auto table_data = ReadBytes(table_path);
    const auto table = ParseTextureTable(table_data);
    EXPECT_EQ(table.size(), 3);
  });
}

//! On the real file system: the data file holds each texture at its aligned
//! offset, as the table records.
NOLINT_TEST_F(
  TextureEmitterRealFileTest, FinalizeWritesTextureDataFileWithAlignment)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    constexpr size_t kPayloadSize1 = 2048;
    constexpr size_t kPayloadSize2 = 1024;
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 8;
    constexpr uint16_t kMipLevels = 4;
    const auto idx0 = emitter.Emit(MakeTestPayload({
                                     .width = kWidth,
                                     .height = kHeight,
                                     .mip_levels = kMipLevels,
                                     .data_size = kPayloadSize1,
                                   }),
      "test_texture");
    const auto idx1 = emitter.Emit(MakeTestPayload({
                                     .width = kWidth,
                                     .height = kHeight,
                                     .mip_levels = kMipLevels,
                                     .data_size = kPayloadSize2,
                                   }),
      "test_texture2");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    const auto data_path = TempDir() / loose_layout::kTexturesData;
    EXPECT_TRUE(std::filesystem::exists(data_path));

    const auto table_path = TempDir() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));

    EXPECT_EQ(table.size(), 3);
    EXPECT_EQ(table.at(0).data_offset, 0);

    const auto aligned_offset1
      = AlignUp(table.at(0).size_bytes, kTextureAlignment);
    const auto aligned_offset2
      = AlignUp(aligned_offset1 + kPayloadSize1, kTextureAlignment);

    EXPECT_EQ(table.at(1).data_offset, aligned_offset1);
    EXPECT_EQ(table.at(1).size_bytes, kPayloadSize1);
    EXPECT_EQ(table.at(2).data_offset, aligned_offset2);
    EXPECT_EQ(table.at(2).size_bytes, kPayloadSize2);

    const auto data_file_size = std::filesystem::file_size(data_path);
    EXPECT_EQ(data_file_size, aligned_offset2 + kPayloadSize2);
  });
}

NOLINT_TEST_F(TextureEmitterTest, FinalizeSerializesTextureMetadataToTable)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    constexpr uint16_t kNumLayers = 6;
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 256;
    constexpr uint16_t kMipLevels = 4;
    constexpr uint16_t kDepth = 1;
    constexpr auto kPayloadSize = kWidth * kHeight * kMipLevels;
    auto payload = MakeTestPayload({
      .width = kWidth,
      .height = kHeight,
      .mip_levels = kMipLevels,
      .data_size = kPayloadSize,
    });
    payload.desc.array_layers = kNumLayers;
    payload.desc.depth = kDepth;
    const auto idx = emitter.Emit(std::move(payload), "test_texture");
    EXPECT_EQ(idx, 1);

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));

    EXPECT_EQ(table.size(), 2);
    EXPECT_EQ(table.at(1).width, kWidth);
    EXPECT_EQ(table.at(1).height, kHeight);
    EXPECT_EQ(table.at(1).mip_levels, kMipLevels);
    EXPECT_EQ(table.at(1).array_layers, kNumLayers);
    EXPECT_EQ(table.at(1).depth, kDepth);
  });
}

NOLINT_TEST_F(TextureEmitterTest, FinalizeWithoutUserTexturesWritesFallback)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    const auto success = co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    EXPECT_TRUE(success);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    EXPECT_TRUE(Exists(table_path));

    const auto data_path = Root() / loose_layout::kTexturesData;
    EXPECT_TRUE(Exists(data_path));

    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 1);
    EXPECT_EQ(table.at(0).data_offset, 0U);
    EXPECT_GT(table.at(0).size_bytes, 0U);

    const auto data_bytes = ReadBytes(data_path);
    const auto data_file_size = data_bytes.size();
    EXPECT_EQ(data_file_size, table.at(0).size_bytes);

    auto payload = oxygen::serio::ByteBuffer::CopyOf(data_bytes);
    if (!payload.has_value()) {
      ADD_FAILURE() << "Cannot hold the payload: " << payload.error().message();
      co_return;
    }

    oxygen::data::TextureResource fallback_resource(
      table.at(0), std::move(payload).value());
    EXPECT_EQ(fallback_resource.GetPayload().size(), table.at(0).size_bytes);
    EXPECT_EQ(fallback_resource.GetPayloadHeader().total_payload_size,
      table.at(0).size_bytes);
    EXPECT_EQ(fallback_resource.GetData().size(),
      fallback_resource.GetSubresourceLayouts().front().size_bytes);
  });
}

//=== State Query Tests ===---------------------------------------------------//

NOLINT_TEST_F(TextureEmitterTest, StatsDataFileSizeTracksAccumulatedSize)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 4;
    constexpr uint16_t kMipLevels = 4;
    constexpr size_t kSize1 = 1000;
    constexpr size_t kSize2 = 500;

    uint64_t size_after_first = 0;
    uint64_t size_after_second = 0;

    EXPECT_EQ(emitter.GetStats().data_file_size, 0);

    const auto idx1 = emitter.Emit(MakeTestPayload({
                                     .width = kWidth,
                                     .height = kHeight,
                                     .mip_levels = kMipLevels,
                                     .data_size = kSize1,
                                   }),
      "test_texture1");
    EXPECT_EQ(idx1, 1);
    size_after_first = emitter.GetStats().data_file_size;
    EXPECT_GT(size_after_first, 0);

    const auto idx2 = emitter.Emit(MakeTestPayload({
                                     .width = kWidth,
                                     .height = kHeight,
                                     .mip_levels = kMipLevels,
                                     .data_size = kSize2,
                                   }),
      "test_texture2");
    EXPECT_EQ(idx2, 2);
    size_after_second = emitter.GetStats().data_file_size;
    EXPECT_GT(size_after_second, size_after_first);

    const auto finalized = co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(finalized);
    EXPECT_TRUE(tables_ok);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto data_path = Root() / loose_layout::kTexturesData;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 3);

    EXPECT_EQ(size_after_first,
      static_cast<uint64_t>(table.at(1).data_offset) + table.at(1).size_bytes);
    EXPECT_EQ(size_after_second,
      static_cast<uint64_t>(table.at(2).data_offset) + table.at(2).size_bytes);
    EXPECT_EQ(size_after_second, SizeOf(data_path));
  });
}

NOLINT_TEST_F(TextureEmitterTest, StatsEmittedTexturesCountsFallbackAndUsers)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    // Assert initial state
    EXPECT_EQ(emitter.GetStats().emitted_textures, 0);

    auto payload0 = MakeTestPayload();
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();
    if (!payload1.payload.empty()) {
      payload1.payload.at(0) ^= std::byte { 0x11 };
    }
    if (!payload2.payload.empty()) {
      payload2.payload.at(0) ^= std::byte { 0x22 };
    }
    const auto idx0 = emitter.Emit(std::move(payload0), "test_texture1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 2);

    const auto idx1 = emitter.Emit(std::move(payload1), "test_texture2");
    const auto idx2 = emitter.Emit(std::move(payload2), "test_texture3");
    EXPECT_EQ(idx1, 2);
    EXPECT_EQ(idx2, 3);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 4);

    const auto success = co_await emitter.Finalize();

    EXPECT_EQ(emitter.GetStats().emitted_textures, 4);
    EXPECT_TRUE(success);
  });
}

NOLINT_TEST_F(TextureEmitterTest, StatsErrorCountStartsAtZero)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    const TextureEmitter emitter(
      writes, TextureAggregator(), MakeEmitterConfig());

    EXPECT_EQ(emitter.GetStats().error_count, 0);
    co_return;
  });
}

//=== Content Verification Tests ===------------------------------------------//

NOLINT_TEST_F(TextureEmitterTest, DataFileWritesPayloadBytes)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();
    // Copy before move
    const std::vector<std::byte> expected_data = payload.payload;

    const auto idx = emitter.Emit(std::move(payload), "test_texture");
    EXPECT_EQ(idx, 1);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_GE(table.size(), 2);

    const auto data_path = Root() / loose_layout::kTexturesData;
    const auto actual_data = ReadBytes(data_path);

    const auto offset = static_cast<size_t>(table.at(1).data_offset);
    EXPECT_LE(offset + expected_data.size(), actual_data.size());
    const auto actual_slice
      = std::span(actual_data).subspan(offset, expected_data.size());
    EXPECT_TRUE(std::equal(
      actual_slice.begin(), actual_slice.end(), expected_data.begin()));
  });
}

NOLINT_TEST_F(TextureEmitterTest, DataFileWritesMultiplePayloadsInOrder)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());

    // Build payloads with expected per-payload patterns.
    std::vector<import::CookedTexturePayload> payloads;
    std::vector<std::vector<std::byte>> expected_payloads;

    for (size_t i = 0; i < 3; ++i) {
      constexpr uint32_t kWidth = 8;
      constexpr uint32_t kHeight = 4;
      constexpr uint16_t kMipLevels = 4;

      const size_t payload_size = 100 + (i * 50);
      auto payload = MakeTestPayload({
        .width = kWidth,
        .height = kHeight,
        .mip_levels = kMipLevels,
        .data_size = payload_size,
      });
      // Fill with distinct pattern based on index
      for (auto& b : payload.payload) {
        constexpr auto kSalt = 10;
        b = static_cast<std::byte>((i + 1) * kSalt);
      }

      expected_payloads.push_back(payload.payload);
      payloads.push_back(std::move(payload));
    }

    // Emit all and finalize in a single Run
    for (size_t i = 0; i < payloads.size(); ++i) {
      const auto key = "test_texture_" + std::to_string(i);
      const auto idx = emitter.Emit(std::move(payloads.at(i)), key);
      EXPECT_EQ(idx, static_cast<uint32_t>(i + 1));
    }
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 1 + expected_payloads.size());

    const auto data_path = Root() / loose_layout::kTexturesData;
    const auto actual_data = ReadBytes(data_path);

    for (size_t i = 0; i < expected_payloads.size(); ++i) {
      const auto& expected = expected_payloads.at(i);
      const auto table_index = i + 1;
      const auto offset
        = static_cast<size_t>(table.at(table_index).data_offset);
      const auto size = static_cast<size_t>(table.at(table_index).size_bytes);
      EXPECT_EQ(size, expected.size());
      EXPECT_LE(offset + size, actual_data.size());
      EXPECT_EQ(offset % kTextureAlignment, 0U);

      const auto actual_slice = std::span(actual_data).subspan(offset, size);
      EXPECT_TRUE(
        std::equal(actual_slice.begin(), actual_slice.end(), expected.begin()));

      if (table_index > 1) {
        const auto prev_end
          = static_cast<uint64_t>(table.at(table_index - 1).data_offset)
          + static_cast<size_t>(table.at(table_index - 1).size_bytes);
        EXPECT_EQ(table.at(table_index).data_offset,
          AlignUp(prev_end, kTextureAlignment));
      }
    }
  });
}

//=== Deduplication Tests ===-------------------------------------------------//

NOLINT_TEST_F(TextureEmitterTest, DedupNoHashIdenticalPayloadAcrossSalts)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();

    const uint32_t idx1 = emitter.Emit(std::move(payload1), "salt_a");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "salt_b");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 1);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 2);
  });
}

NOLINT_TEST_F(
  TextureEmitterTest, CollisionPolicyWarnKeepFirstExpectedToKeepIndex)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    std::vector<import::ImportDiagnostic> diagnostics;
    auto config = MakeEmitterConfig();
    config.collision_policy = import::DedupCollisionPolicy::kWarnKeepFirst;
    config.on_dedup_diagnostic
      = [&](import::ImportDiagnostic diagnostic) -> void {
      diagnostics.push_back(std::move(diagnostic));
    };
    TextureEmitter emitter(writes, TextureAggregator(), std::move(config));
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();
    EXPECT_FALSE(payload2.payload.empty());
    payload2.payload.at(0) ^= std::byte { 0xFF };

    const uint32_t idx1 = emitter.Emit(std::move(payload1), "same_salt");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "same_salt");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 1);
    EXPECT_EQ(diagnostics.size(), 1U);
    if (diagnostics.size() == 1U) {
      EXPECT_EQ(diagnostics.front().code, "import.dedup_collision.texture");
      EXPECT_EQ(diagnostics.front().severity, import::ImportSeverity::kWarning);
    }

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 2);
    co_return;
  });
}

NOLINT_TEST_F(TextureEmitterTest, CollisionPolicyWarnReplaceExpectedToEmitNew)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    std::vector<import::ImportDiagnostic> diagnostics;
    auto config = MakeEmitterConfig();
    config.collision_policy = import::DedupCollisionPolicy::kWarnReplace;
    config.on_dedup_diagnostic
      = [&](import::ImportDiagnostic diagnostic) -> void {
      diagnostics.push_back(std::move(diagnostic));
    };
    TextureEmitter emitter(writes, TextureAggregator(), std::move(config));
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();
    EXPECT_FALSE(payload2.payload.empty());
    payload2.payload.at(0) ^= std::byte { 0x0F };

    const uint32_t idx1 = emitter.Emit(std::move(payload1), "same_salt");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "same_salt");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);
    EXPECT_EQ(diagnostics.size(), 1U);
    if (diagnostics.size() == 1U) {
      EXPECT_EQ(diagnostics.front().code, "import.dedup_collision.texture");
      EXPECT_EQ(diagnostics.front().severity, import::ImportSeverity::kWarning);
    }
    co_return;
  });
}

NOLINT_TEST_F(TextureEmitterTest, CollisionPolicyErrorExpectedToThrow)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    auto config = MakeEmitterConfig();
    config.collision_policy = import::DedupCollisionPolicy::kError;
    TextureEmitter emitter(writes, TextureAggregator(), std::move(config));
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();
    EXPECT_FALSE(payload2.payload.empty());
    payload2.payload.at(0) ^= std::byte { 0xF0 };

    (void)emitter.Emit(std::move(payload1), "same_salt");
    EXPECT_THROW(
      (void)emitter.Emit(std::move(payload2), "same_salt"), std::runtime_error);
    co_return;
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitWithHashSaltIgnoredIdenticalContent)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload1 = MakeTestPayload(
      {
        .width = kDefaultTextureWidth,
        .height = kDefaultTextureHeight,
        .mip_levels = kDefaultTextureMips,
        .data_size = kDefaultTextureDataSize,
      },
      true);
    auto payload2 = MakeTestPayload(
      {
        .width = kDefaultTextureWidth,
        .height = kDefaultTextureHeight,
        .mip_levels = kDefaultTextureMips,
        .data_size = kDefaultTextureDataSize,
      },
      true);

    const uint32_t idx1 = emitter.Emit(std::move(payload1), "salt_a");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "salt_b");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 1);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 2);
  });
}

NOLINT_TEST_F(TextureEmitterTest, EmitWithHashSameSaltDifferentContent)
{
  RunInNursery([&](co::Nursery& writes) -> co::Co<> {
    TextureEmitter emitter(writes, TextureAggregator(), MakeEmitterConfig());
    auto payload1 = MakeTestPayload(
      {
        .width = kDefaultTextureWidth,
        .height = kDefaultTextureHeight,
        .mip_levels = kDefaultTextureMips,
        .data_size = kDefaultTextureDataSize,
      },
      true);
    auto payload2 = payload1;
    EXPECT_FALSE(payload2.payload.empty());
    constexpr auto kAllBitsSet = std::numeric_limits<uint8_t>::max();
    payload2.payload.at(0) ^= static_cast<std::byte>(kAllBitsSet);
    payload2.desc.content_hash
      = import::util::ComputeContentHash(payload2.payload);
    if (payload2.desc.content_hash == 0) {
      payload2.desc.content_hash = 1;
    }

    const uint32_t idx1 = emitter.Emit(std::move(payload1), "same_salt");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "same_salt");
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);

    const auto table_path = Root() / loose_layout::kTexturesTable;
    const auto table = ParseTextureTable(ReadBytes(table_path));
    EXPECT_EQ(table.size(), 3);
  });
}

} // namespace
