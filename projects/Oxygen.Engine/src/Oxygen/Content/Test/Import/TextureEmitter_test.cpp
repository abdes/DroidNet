//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Content/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/OxCo/Run.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
using PakTextureResourceDesc = oxygen::data::pak::TextureResourceDesc;
namespace co = oxygen::co;

namespace {

// Alignment used by TextureEmitter (matches kRowPitchAlignment)
constexpr uint64_t kTextureAlignment = 256;

//! Aligns a value up to the alignment boundary.
constexpr auto AlignUp(uint64_t value, uint64_t alignment) -> uint64_t
{
  if (alignment <= 1)
    return value;
  const auto remainder = value % alignment;
  return (remainder == 0) ? value : (value + (alignment - remainder));
}

//=== Test Helpers ===--------------------------------------------------------//

//! Create a test cooked texture payload with specified dimensions.
auto MakeTestPayload(uint32_t width, uint32_t height, uint16_t mip_levels = 1,
  size_t data_size = 1024) -> CookedTexturePayload
{
  CookedTexturePayload payload;
  payload.desc.width = width;
  payload.desc.height = height;
  payload.desc.mip_levels = mip_levels;
  payload.desc.depth = 1;
  payload.desc.array_layers = 1;
  payload.desc.texture_type = oxygen::TextureType::kTexture2D;
  payload.desc.format = oxygen::Format::kBC7UNorm;
  payload.desc.content_hash = 0x12345678ABCDEF00ULL;

  // Fill payload with recognizable pattern
  payload.payload.resize(data_size);
  for (size_t i = 0; i < data_size; ++i) {
    payload.payload[i] = static_cast<std::byte>(i & 0xFF);
  }

  return payload;
}

//! Read binary file content.
auto ReadBinaryFile(const std::filesystem::path& path) -> std::vector<std::byte>
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const auto size = file.tellg();
  file.seekg(0);
  std::vector<std::byte> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

//! Parse texture table from binary data.
auto ParseTextureTable(const std::vector<std::byte>& data)
  -> std::vector<PakTextureResourceDesc>
{
  const auto count = data.size() / sizeof(PakTextureResourceDesc);
  std::vector<PakTextureResourceDesc> table(count);
  std::memcpy(
    table.data(), data.data(), count * sizeof(PakTextureResourceDesc));
  return table;
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for TextureEmitter tests.
class TextureEmitterTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
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

  auto Layout() const -> const LooseCookedLayout& { return layout_; }

  auto MakeEmitterConfig() const -> TextureEmitter::Config
  {
    return TextureEmitter::Config {
      .cooked_root = test_dir_,
      .layout = layout_,
      .packing_policy_id = "d3d12",
      .data_alignment = kTextureAlignment,
    };
  }

  auto TextureAggregator() -> TextureTableAggregator&
  {
    return table_registry_->TextureAggregator(test_dir_, layout_);
  }

  auto FinalizeTables() -> bool
  {
    bool ok = false;
    co::Run(*loop_, [&]() -> Co<> {
      ok = co_await table_registry_->FinalizeAll();
      co_return;
    });
    return ok;
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  std::filesystem::path test_dir_;
  LooseCookedLayout layout_ {}; // Uses default paths
};

//=== Basic Emission Tests
//===-------------------------------------------------//

//! Verify emitting a single texture returns index 0.
NOLINT_TEST_F(TextureEmitterTest, Emit_SingleTexture_ReturnsIndexOne)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto payload = MakeTestPayload(512, 512);

  // Act
  uint32_t index = 0;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    index = emitter.Emit(std::move(payload));

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(index, 1);
  EXPECT_EQ(emitter.GetStats().emitted_textures, 2);
  EXPECT_TRUE(success);
}

//! Verify emitting multiple textures returns sequential indices.
NOLINT_TEST_F(
  TextureEmitterTest, Emit_MultipleTextures_ReturnsSequentialIndices)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Act
  std::vector<uint32_t> indices;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < 5; ++i) {
      auto payload = MakeTestPayload(256, 256, 1, 512 + i * 100);
      indices.push_back(emitter.Emit(std::move(payload)));
    }

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(indices.size(), 5);
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(indices[i], i + 1);
  }
  EXPECT_EQ(emitter.GetStats().emitted_textures, 6);
  EXPECT_TRUE(success);
}

//! Verify index is returned immediately before I/O completes.
NOLINT_TEST_F(TextureEmitterTest, Emit_ReturnsImmediately_BeforeIOCompletes)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto payload = MakeTestPayload(1024, 1024, 8, 4 * 1024); // 4KB

  // Act
  uint32_t index = 0;
  bool had_pending = false;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    index = emitter.Emit(std::move(payload));
    had_pending = emitter.GetStats().pending_writes > 0;

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(index, 1); // Index assigned immediately
  EXPECT_TRUE(had_pending); // I/O was queued
  EXPECT_TRUE(success);
}

//! Verify emitting after Finalize() is rejected.
NOLINT_TEST_F(TextureEmitterTest, Emit_AfterFinalize_Throws)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Act & Assert
  co::Run(*loop_, [&]() -> Co<> {
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW((void)emitter.Emit(MakeTestPayload(256, 256, 1, 512)),
      std::runtime_error);
  });
}

//=== Finalization Tests
//===---------------------------------------------------//

//! Verify finalization waits for pending I/O.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WaitsForPendingIO)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto idx0 = emitter.Emit(MakeTestPayload(512, 512, 4, 2048));
  auto idx1 = emitter.Emit(MakeTestPayload(256, 256, 2, 1024));
  EXPECT_EQ(idx0, 1);
  EXPECT_EQ(idx1, 2);

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> { success = co_await emitter.Finalize(); });

  // Assert
  EXPECT_TRUE(success);
  EXPECT_EQ(emitter.GetStats().pending_writes, 0);
  EXPECT_EQ(emitter.GetStats().error_count, 0);
}

//! Verify finalization writes table file with correct entries.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WritesTableFile)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto idx0 = emitter.Emit(MakeTestPayload(512, 512, 4, 2048));
  auto idx1 = emitter.Emit(MakeTestPayload(256, 256, 2, 1024));
  EXPECT_EQ(idx0, 1);
  EXPECT_EQ(idx1, 2);

  // Act
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  EXPECT_TRUE(std::filesystem::exists(table_path));

  const auto table_data = ReadBinaryFile(table_path);
  const auto table = ParseTextureTable(table_data);
  EXPECT_EQ(table.size(), 3);
}

//! Verify finalization writes data file with aligned content.
NOLINT_TEST_F(TextureEmitterTest, Finalize_WritesDataFile)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  constexpr size_t kPayloadSize1 = 2048;
  constexpr size_t kPayloadSize2 = 1024;
  auto idx0 = emitter.Emit(MakeTestPayload(512, 512, 4, kPayloadSize1));
  auto idx1 = emitter.Emit(MakeTestPayload(256, 256, 2, kPayloadSize2));
  EXPECT_EQ(idx0, 1);
  EXPECT_EQ(idx1, 2);

  // Act
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
  EXPECT_TRUE(std::filesystem::exists(data_path));

  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 3);
  EXPECT_EQ(table[0].data_offset, 0);

  const auto aligned_offset1 = AlignUp(table[0].size_bytes, kTextureAlignment);
  const auto aligned_offset2
    = AlignUp(aligned_offset1 + kPayloadSize1, kTextureAlignment);

  EXPECT_EQ(table[1].data_offset, aligned_offset1);
  EXPECT_EQ(table[1].size_bytes, kPayloadSize1);
  EXPECT_EQ(table[2].data_offset, aligned_offset2);
  EXPECT_EQ(table[2].size_bytes, kPayloadSize2);

  const auto data_file_size = std::filesystem::file_size(data_path);
  EXPECT_EQ(data_file_size, aligned_offset2 + kPayloadSize2);
}

//! Verify table entries have correctly aligned offsets.
NOLINT_TEST_F(TextureEmitterTest, Finalize_TableEntriesHaveCorrectOffsets)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  constexpr size_t kPayloadSize1 = 2048;
  constexpr size_t kPayloadSize2 = 1024;
  auto idx0 = emitter.Emit(MakeTestPayload(512, 512, 4, kPayloadSize1));
  auto idx1 = emitter.Emit(MakeTestPayload(256, 256, 2, kPayloadSize2));
  EXPECT_EQ(idx0, 1);
  EXPECT_EQ(idx1, 2);

  // Act
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 3);
  EXPECT_EQ(table[0].data_offset, 0);

  const auto aligned_offset1 = AlignUp(table[0].size_bytes, kTextureAlignment);
  const auto aligned_offset2
    = AlignUp(aligned_offset1 + kPayloadSize1, kTextureAlignment);

  EXPECT_EQ(table[1].data_offset, aligned_offset1);
  EXPECT_EQ(table[1].size_bytes, kPayloadSize1);
  EXPECT_EQ(table[2].data_offset, aligned_offset2);
  EXPECT_EQ(table[2].size_bytes, kPayloadSize2);
}

//! Verify table entries preserve texture metadata.
NOLINT_TEST_F(TextureEmitterTest, Finalize_TableEntriesPreserveMetadata)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto payload = MakeTestPayload(512, 256, 4, 2048);
  payload.desc.array_layers = 6;
  payload.desc.depth = 1;
  auto idx = emitter.Emit(std::move(payload));
  EXPECT_EQ(idx, 1);

  // Act
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 2);
  EXPECT_EQ(table[1].width, 512);
  EXPECT_EQ(table[1].height, 256);
  EXPECT_EQ(table[1].mip_levels, 4);
  EXPECT_EQ(table[1].array_layers, 6);
  EXPECT_EQ(table[1].depth, 1);
}

//! Verify finalization with no textures still writes the fallback entry.
NOLINT_TEST_F(TextureEmitterTest, Finalize_NoTextures_WritesFallback)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Act
  bool success = false;
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    success = co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  EXPECT_TRUE(success);

  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  EXPECT_TRUE(std::filesystem::exists(table_path));

  const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
  EXPECT_TRUE(std::filesystem::exists(data_path));

  const auto table = ParseTextureTable(ReadBinaryFile(table_path));
  ASSERT_EQ(table.size(), 1);
  EXPECT_EQ(table[0].data_offset, 0U);
  EXPECT_GT(table[0].size_bytes, 0U);

  const auto data_file_size = std::filesystem::file_size(data_path);
  EXPECT_EQ(data_file_size, table[0].size_bytes);
}

//=== State Query Tests
//===----------------------------------------------------//

//! Verify DataFileSize tracks accumulated data with alignment.
NOLINT_TEST_F(TextureEmitterTest, DataFileSize_TracksAccumulatedSize)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  constexpr size_t kSize1 = 1000;
  constexpr size_t kSize2 = 500;

  uint64_t size_after_first = 0;
  uint64_t size_after_second = 0;
  bool finalized = false;
  bool tables_ok = false;

  // Act & Assert
  EXPECT_EQ(emitter.GetStats().data_file_size, 0);

  co::Run(*loop_, [&]() -> Co<> {
    auto idx1 = emitter.Emit(MakeTestPayload(512, 512, 1, kSize1));
    EXPECT_EQ(idx1, 1);
    size_after_first = emitter.GetStats().data_file_size;
    EXPECT_GT(size_after_first, 0);

    auto idx2 = emitter.Emit(MakeTestPayload(256, 256, 1, kSize2));
    EXPECT_EQ(idx2, 2);
    size_after_second = emitter.GetStats().data_file_size;
    EXPECT_GT(size_after_second, size_after_first);

    finalized = co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(finalized);
  EXPECT_TRUE(tables_ok);

  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));
  ASSERT_EQ(table.size(), 3);

  EXPECT_EQ(size_after_first,
    static_cast<uint64_t>(table[1].data_offset) + table[1].size_bytes);
  EXPECT_EQ(size_after_second,
    static_cast<uint64_t>(table[2].data_offset) + table[2].size_bytes);
  EXPECT_EQ(size_after_second, std::filesystem::file_size(data_path));
}

//! Verify Count returns number of emitted textures.
NOLINT_TEST_F(TextureEmitterTest, Count_ReturnsNumberOfEmittedTextures)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    // Assert initial state
    EXPECT_EQ(emitter.GetStats().emitted_textures, 0);

    auto idx0 = emitter.Emit(MakeTestPayload(512, 512));
    EXPECT_EQ(idx0, 1);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 2);

    auto idx1 = emitter.Emit(MakeTestPayload(256, 256));
    auto idx2 = emitter.Emit(MakeTestPayload(128, 128));
    EXPECT_EQ(idx1, 2);
    EXPECT_EQ(idx2, 3);
    EXPECT_EQ(emitter.GetStats().emitted_textures, 4);

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(emitter.GetStats().emitted_textures, 4);
  EXPECT_TRUE(success);
}

//! Verify ErrorCount starts at zero.
NOLINT_TEST_F(TextureEmitterTest, ErrorCount_InitiallyZero)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Assert
  EXPECT_EQ(emitter.GetStats().error_count, 0);
}

//=== Content Verification Tests ===------------------------------------------//

//! Verify data file content matches emitted payload.
NOLINT_TEST_F(TextureEmitterTest, DataFileContent_MatchesEmittedPayload)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto payload = MakeTestPayload(256, 256, 1, 128);
  const auto expected_data = payload.payload; // Copy before move

  // Act
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto idx = emitter.Emit(std::move(payload));
    EXPECT_EQ(idx, 1);
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));
  ASSERT_GE(table.size(), 2);

  const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
  const auto actual_data = ReadBinaryFile(data_path);

  const auto offset = static_cast<size_t>(table[1].data_offset);
  ASSERT_LE(offset + expected_data.size(), actual_data.size());
  const auto actual_slice
    = std::span(actual_data).subspan(offset, expected_data.size());
  EXPECT_TRUE(std::equal(
    actual_slice.begin(), actual_slice.end(), expected_data.begin()));
}

//! Verify multiple payloads are written with correct alignment padding.
NOLINT_TEST_F(TextureEmitterTest, MultiplePayloads_ConcatenatedInOrder)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());

  // Build payloads with expected per-payload patterns.
  std::vector<CookedTexturePayload> payloads;
  std::vector<std::vector<std::byte>> expected_payloads;

  for (int i = 0; i < 3; ++i) {
    const size_t payload_size = 100 + static_cast<size_t>(i) * 50;
    auto payload = MakeTestPayload(128, 128, 1, payload_size);
    // Fill with distinct pattern based on index
    for (auto& b : payload.payload) {
      b = static_cast<std::byte>((i + 1) * 10);
    }

    expected_payloads.push_back(payload.payload);
    payloads.push_back(std::move(payload));
  }

  // Act - emit all and finalize in a single Run
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (size_t i = 0; i < payloads.size(); ++i) {
      auto idx = emitter.Emit(std::move(payloads[i]));
      EXPECT_EQ(idx, static_cast<uint32_t>(i + 1));
    }
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));
  ASSERT_EQ(table.size(), 1 + expected_payloads.size());

  const auto data_path = test_dir_ / Layout().TexturesDataRelPath();
  const auto actual_data = ReadBinaryFile(data_path);

  for (size_t i = 0; i < expected_payloads.size(); ++i) {
    const auto& expected = expected_payloads[i];
    const auto table_index = i + 1;
    const auto offset = static_cast<size_t>(table[table_index].data_offset);
    const auto size = static_cast<size_t>(table[table_index].size_bytes);
    ASSERT_EQ(size, expected.size());
    ASSERT_LE(offset + size, actual_data.size());
    EXPECT_EQ(offset % kTextureAlignment, 0U);

    const auto actual_slice = std::span(actual_data).subspan(offset, size);
    EXPECT_TRUE(
      std::equal(actual_slice.begin(), actual_slice.end(), expected.begin()));

    if (table_index > 1) {
      const auto prev_end
        = static_cast<uint64_t>(table[table_index - 1].data_offset)
        + table[table_index - 1].size_bytes;
      EXPECT_EQ(
        table[table_index].data_offset, AlignUp(prev_end, kTextureAlignment));
    }
  }
}

//! Verify identical textures are deduplicated by signature.
NOLINT_TEST_F(TextureEmitterTest, Emit_IdenticalTextures_ReusesIndex)
{
  // Arrange
  TextureEmitter emitter(*writer_, TextureAggregator(), MakeEmitterConfig());
  auto payload1 = MakeTestPayload(256, 256, 1, 256);
  auto payload2 = MakeTestPayload(256, 256, 1, 256);

  // Act
  uint32_t idx1 = 0;
  uint32_t idx2 = 0;
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    idx1 = emitter.Emit(std::move(payload1));
    idx2 = emitter.Emit(std::move(payload2));
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  EXPECT_EQ(idx1, 1);
  EXPECT_EQ(idx2, 1);
  EXPECT_EQ(emitter.GetStats().emitted_textures, 2);

  const auto table_path = test_dir_ / Layout().TexturesTableRelPath();
  const auto table = ParseTextureTable(ReadBinaryFile(table_path));
  EXPECT_EQ(table.size(), 2);
}

} // namespace
