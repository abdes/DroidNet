//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Content/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>

using PakTextureResourceDesc = oxygen::data::pak::TextureResourceDesc;
namespace import = oxygen::content::import;
namespace co = oxygen::co;

namespace {

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

//! Read binary file content.
auto ReadBinaryFile(const std::filesystem::path& path) -> std::vector<std::byte>
{
  std::basic_ifstream<std::byte> file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }

  const auto end = file.tellg();
  if (end <= std::basic_ifstream<std::byte>::pos_type { 0 }) {
    return {};
  }

  file.seekg(0);

  const auto size = static_cast<std::size_t>(end);
  std::vector<std::byte> data(size);

  file.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!file) {
    return {};
  }

  return data;
}

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

//! Test fixture for TextureEmitter tests.
class TextureEmitterTest : public testing::Test {
protected:
  // Local type aliases so member signatures can use unqualified names.
  using LooseCookedLayout = import::LooseCookedLayout;
  using TextureEmitter = import::TextureEmitter;
  using TextureTableAggregator = import::TextureTableAggregator;
  using ImportEventLoop = import::ImportEventLoop;
  using FileWriter = import::WindowsFileWriter;
  using ResourceTableRegistry = import::ResourceTableRegistry;

  struct PayloadProps {
    uint32_t width;
    uint32_t height;
    uint16_t mip_levels;
    size_t data_size;
  };

  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<FileWriter>(*loop_);
    table_registry_ = std::make_unique<ResourceTableRegistry>(*writer_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_texture_emitter_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    table_registry_.reset();
    writer_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  [[nodiscard]] auto Layout() const -> const LooseCookedLayout&
  {
    return layout_;
  }

  [[nodiscard]] auto MakeEmitterConfig() const -> TextureEmitter::Config
  {
    return TextureEmitter::Config {
      .cooked_root = test_dir_,
      .layout = layout_,
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

    // Default to no hashing so deduplication can be controlled via the
    // Emit() signature salt in tests.
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

  [[nodiscard]] auto TextureAggregator() const -> TextureTableAggregator&
  {
    return table_registry_->TextureAggregator(test_dir_, layout_);
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  static constexpr uint32_t kDefaultTextureWidth = 4;
  static constexpr uint32_t kDefaultTextureHeight = 4;
  static constexpr uint16_t kDefaultTextureMips = 1;
  static constexpr size_t kDefaultTextureDataSize = 128;

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<FileWriter> writer_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  std::filesystem::path test_dir_;
  LooseCookedLayout layout_ {}; // Uses default paths
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

//=== Basic Emission Tests ===------------------------------------------------//

//! Verify the first emitted user texture gets index 1.
NOLINT_TEST_F(TextureEmitterTest, Emit_SingleTexture_AssignsFirstIndex)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();

    // Act
    const auto index = emitter.Emit(std::move(payload), "test_texture");
    const auto success = co_await emitter.Finalize();

    // Assert
    EXPECT_EQ(index, 1);
    EXPECT_TRUE(success);
  });
}

//! Verify multiple unique textures receive sequential indices.
NOLINT_TEST_F(TextureEmitterTest, Emit_UniqueTextures_AssignsSequentialIndices)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

    // Act
    constexpr std::array<std::string_view, 5> kSalts {
      "t0",
      "t1",
      "t2",
      "t3",
      "t4",
    };
    std::vector<uint32_t> indices;
    indices.reserve(kSalts.size());
    for (const auto salt : kSalts) {
      auto payload = MakeTestPayload();
      indices.push_back(emitter.Emit(std::move(payload), salt));
    }

    const bool success = co_await emitter.Finalize();

    // Assert
    EXPECT_EQ(indices.size(), kSalts.size());
    for (size_t i = 0; i < kSalts.size(); ++i) {
      EXPECT_EQ(indices.at(i), i + 1);
    }
    EXPECT_TRUE(success);
  });
}

//! Verify index is returned immediately before I/O completes.
NOLINT_TEST_F(TextureEmitterTest, Emit_QueuesWrite_ReturnsBeforeFinalize)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();

    // Act
    const uint32_t index = emitter.Emit(std::move(payload), "test_texture");
    const bool had_pending = emitter.GetStats().pending_writes > 0;
    const bool success = co_await emitter.Finalize();

    // Assert
    EXPECT_EQ(index, 1);
    EXPECT_TRUE(had_pending);
    EXPECT_TRUE(success);
  });
}

//! Verify emitting after Finalize() is rejected.
NOLINT_TEST_F(TextureEmitterTest, Emit_AfterFinalize_Throws)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

    // Act & Assert
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    NOLINT_EXPECT_THROW((void)emitter.Emit(MakeTestPayload(), "test_texture"),
      std::runtime_error);
  });
}

//=== Finalization Tests ===--------------------------------------------------//

//! Verify finalization drains all pending writes.
NOLINT_TEST_F(TextureEmitterTest, Finalize_DrainsPendingWrites)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

    // Arrange
    (void)emitter.Emit(MakeTestPayload(), "t0");
    (void)emitter.Emit(MakeTestPayload(), "t1");
    EXPECT_GT(emitter.GetStats().pending_writes, 0U);

    // Act
    const bool success = co_await emitter.Finalize();

    // Assert
    EXPECT_TRUE(success);
    EXPECT_EQ(emitter.GetStats().pending_writes, 0);
    EXPECT_EQ(emitter.GetStats().error_count, 0);
  });
}

//! Verify finalization writes table file with correct entries.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WritesTextureTableFile)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    const auto idx0 = emitter.Emit(MakeTestPayload(), "t0");
    const auto idx1 = emitter.Emit(MakeTestPayload(), "t1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(idx1, 2);

    // Act
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    EXPECT_TRUE(std::filesystem::exists(table_path));

    const auto table_data = ReadBinaryFile(table_path);
    const auto table = ParseTextureTable(table_data);
    EXPECT_EQ(table.size(), 3);
  });
}

//! Verify finalization writes data file with aligned content.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WritesTextureDataFile_WithAlignment)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
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

    // Act
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
    EXPECT_TRUE(std::filesystem::exists(data_path));

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));

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

//! Verify table entries preserve texture metadata.
NOLINT_TEST_F(TextureEmitterTest, Finalize_SerializesTextureMetadataToTable)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
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

    // Act
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));

    EXPECT_EQ(table.size(), 2);
    EXPECT_EQ(table.at(1).width, kWidth);
    EXPECT_EQ(table.at(1).height, kHeight);
    EXPECT_EQ(table.at(1).mip_levels, kMipLevels);
    EXPECT_EQ(table.at(1).array_layers, kNumLayers);
    EXPECT_EQ(table.at(1).depth, kDepth);
  });
}

//! Verify finalization with no textures still writes the fallback entry.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WithoutUserTextures_WritesFallback)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

    // Act
    const bool success = co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    EXPECT_TRUE(success);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    EXPECT_TRUE(std::filesystem::exists(table_path));

    const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
    EXPECT_TRUE(std::filesystem::exists(data_path));

    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 1);
    EXPECT_EQ(table.at(0).data_offset, 0U);
    EXPECT_GT(table.at(0).size_bytes, 0U);

    const auto data_file_size = std::filesystem::file_size(data_path);
    EXPECT_EQ(data_file_size, table.at(0).size_bytes);
  });
}

//=== State Query Tests ===---------------------------------------------------//

//! Verify DataFileSize tracks accumulated data with alignment.
NOLINT_TEST_F(TextureEmitterTest, Stats_DataFileSize_TracksAccumulatedSize)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 4;
    constexpr uint16_t kMipLevels = 4;
    constexpr size_t kSize1 = 1000;
    constexpr size_t kSize2 = 500;

    uint64_t size_after_first = 0;
    uint64_t size_after_second = 0;

    // Act & Assert
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

    const bool finalized = co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(finalized);
    EXPECT_TRUE(tables_ok);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 3);

    EXPECT_EQ(size_after_first,
      static_cast<uint64_t>(table.at(1).data_offset) + table.at(1).size_bytes);
    EXPECT_EQ(size_after_second,
      static_cast<uint64_t>(table.at(2).data_offset) + table.at(2).size_bytes);
    EXPECT_EQ(size_after_second, std::filesystem::file_size(data_path));
  });
}

//! Verify Count returns number of emitted textures.
NOLINT_TEST_F(TextureEmitterTest, Stats_EmittedTextures_CountsFallbackAndUsers)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

    // Act
    // Assert initial state
    EXPECT_EQ(emitter.GetStats().emitted_textures, 0);

    const auto idx0 = emitter.Emit(MakeTestPayload(), "test_texture1");
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 2);

    const auto idx1 = emitter.Emit(MakeTestPayload(), "test_texture2");
    const auto idx2 = emitter.Emit(MakeTestPayload(), "test_texture3");
    EXPECT_EQ(idx1, 2);
    EXPECT_EQ(idx2, 3);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 4);

    const bool success = co_await emitter.Finalize();

    // Assert
    EXPECT_EQ(emitter.GetStats().emitted_textures, 4);
    EXPECT_TRUE(success);
  });
}

//! Verify ErrorCount starts at zero.
NOLINT_TEST_F(TextureEmitterTest, Stats_ErrorCount_StartsAtZero)
{
  // Arrange
  const TextureEmitter emitter(
    *writer_, TextureAggregator(), MakeEmitterConfig());

  // Assert
  EXPECT_EQ(emitter.GetStats().error_count, 0);
}

//=== Content Verification Tests ===------------------------------------------//

//! Verify data file content matches emitted payload.
NOLINT_TEST_F(TextureEmitterTest, DataFile_WritesPayloadBytes)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    auto payload = MakeTestPayload();
    // Copy before move
    const std::vector<std::byte> expected_data = payload.payload;

    // Act
    const auto idx = emitter.Emit(std::move(payload), "test_texture");
    EXPECT_EQ(idx, 1);
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_GE(table.size(), 2);

    const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
    const auto actual_data = ReadBinaryFile(data_path);

    const auto offset = static_cast<size_t>(table.at(1).data_offset);
    EXPECT_LE(offset + expected_data.size(), actual_data.size());
    const auto actual_slice
      = std::span(actual_data).subspan(offset, expected_data.size());
    EXPECT_TRUE(std::equal(
      actual_slice.begin(), actual_slice.end(), expected_data.begin()));
  });
}

//! Verify multiple payloads are written with correct alignment padding.
NOLINT_TEST_F(TextureEmitterTest, DataFile_WritesMultiplePayloads_InOrder)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

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

    // Act - emit all and finalize in a single Run
    for (size_t i = 0; i < payloads.size(); ++i) {
      const auto idx = emitter.Emit(std::move(payloads.at(i)), "test_texture");
      EXPECT_EQ(idx, static_cast<uint32_t>(i + 1));
    }
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();
    EXPECT_TRUE(tables_ok);

    // Assert
    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 1 + expected_payloads.size());

    const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
    const auto actual_data = ReadBinaryFile(data_path);

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

//! Verify with no content hashing, different salts do not collide.
NOLINT_TEST_F(TextureEmitterTest, Dedup_NoHash_DifferentSalts_NoCollision)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();

    // Act
    const uint32_t idx1 = emitter.Emit(std::move(payload1), "salt_a");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "salt_b");
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    // Assert
    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 3);
  });
}

//! Verify with no content hashing, same salt causes collision.
NOLINT_TEST_F(TextureEmitterTest, Dedup_NoHash_SameSalt_Collision)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
    auto payload1 = MakeTestPayload();
    auto payload2 = MakeTestPayload();

    // Act
    const uint32_t idx1 = emitter.Emit(std::move(payload1), "same_salt");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "same_salt");
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    // Assert
    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 1);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 2);
  });
}

//! Verify with content hashing enabled, salt is ignored and identical content
//! collides.
NOLINT_TEST_F(TextureEmitterTest, Emit_WithHash_SaltIgnored_IdenticalContent)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
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

    // Act
    const uint32_t idx1 = emitter.Emit(std::move(payload1), "salt_a");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "salt_b");
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    // Assert
    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 1);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 2);
  });
}

//! Verify with content hashing enabled, different content does not collide even
//! with the same salt.
NOLINT_TEST_F(TextureEmitterTest, Emit_WithHash_SameSalt_DifferentContent)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
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

    // Act
    const uint32_t idx1 = emitter.Emit(std::move(payload1), "same_salt");
    const uint32_t idx2 = emitter.Emit(std::move(payload2), "same_salt");
    co_await emitter.Finalize();
    const bool tables_ok = co_await table_registry_->FinalizeAll();

    // Assert
    EXPECT_TRUE(tables_ok);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);

    const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
    const auto table = ParseTextureTable(ReadBinaryFile(table_path));
    EXPECT_EQ(table.size(), 3);
  });
}

} // namespace
