//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Content/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/OxCo/Run.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
using PakBufferResourceDesc = oxygen::data::pak::BufferResourceDesc;
namespace co = oxygen::co;

namespace {

//! Aligns a value up to the alignment boundary.
constexpr auto AlignUp(uint64_t value, uint64_t alignment) -> uint64_t
{
  if (alignment <= 1)
    return value;
  const auto remainder = value % alignment;
  return (remainder == 0) ? value : (value + (alignment - remainder));
}

//=== Test Helpers
//===---------------------------------------------------------//

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
  payload.content_hash = (static_cast<uint64_t>(fill_u8) << 56)
    ^ (static_cast<uint64_t>(alignment) << 24)
    ^ (static_cast<uint64_t>(usage_flags) << 12)
    ^ (static_cast<uint64_t>(element_stride) << 4)
    ^ static_cast<uint64_t>(size_bytes);

  // Fill payload with recognizable pattern
  payload.data.resize(size_bytes);
  for (size_t i = 0; i < size_bytes; ++i) {
    // Mix fill_byte with position for unique content
    payload.data[i]
      = static_cast<std::byte>(static_cast<uint8_t>(fill_byte) ^ (i & 0xFF));
  }

  return payload;
}

//! Create a vertex buffer payload (usage=0x01, alignment=16).
auto MakeVertexBuffer(size_t size_bytes, uint32_t stride = 32)
  -> CookedBufferPayload
{
  auto payload
    = MakeTestBuffer(size_bytes, 0x01, 16, stride, std::byte { 0xAA });
  return payload;
}

//! Create an index buffer payload (usage=0x02, alignment=4).
auto MakeIndexBuffer(size_t size_bytes) -> CookedBufferPayload
{
  return MakeTestBuffer(size_bytes, 0x02, 4, 0, std::byte { 0x1B });
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

//! Parse buffer table from binary data.
/*!
 Deserializes the packed BufferResourceDesc entries from the table file.
 Each entry is 32 bytes as per PAK format specification.
*/
auto ParseBufferTable(const std::vector<std::byte>& data)
  -> std::vector<PakBufferResourceDesc>
{
  static_assert(sizeof(PakBufferResourceDesc) == 32,
    "BufferResourceDesc must be 32 bytes per PAK format");

  const auto count = data.size() / sizeof(PakBufferResourceDesc);
  std::vector<PakBufferResourceDesc> table(count);
  std::memcpy(table.data(), data.data(), count * sizeof(PakBufferResourceDesc));
  return table;
}

//! Verify buffer data content at a given offset in the data file.
auto VerifyBufferContent(const std::vector<std::byte>& data_file,
  uint64_t offset, const std::vector<std::byte>& expected) -> bool
{
  if (offset + expected.size() > data_file.size()) {
    return false;
  }
  return std::memcmp(
           data_file.data() + offset, expected.data(), expected.size())
    == 0;
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for BufferEmitter tests.
class BufferEmitterTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
    table_registry_ = std::make_unique<ResourceTableRegistry>(*writer_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_buffer_emitter_test";
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

  auto BufferAggregator() -> BufferTableAggregator&
  {
    return table_registry_->BufferAggregator(test_dir_, layout_);
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

//! Verify emitting a single buffer returns index 0.
NOLINT_TEST_F(BufferEmitterTest, Emit_SingleBuffer_ReturnsIndexZero)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);
  auto payload = MakeTestBuffer(1024, 0x01, 16, 32);

  // Act
  uint32_t index = 0;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    index = emitter.Emit(std::move(payload), "buf0");

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(index, 0);
  EXPECT_EQ(emitter.Count(), 1);
  EXPECT_TRUE(success);
}

//! Verify emitting multiple buffers returns sequential indices.
NOLINT_TEST_F(BufferEmitterTest, Emit_MultipleBuffers_ReturnsSequentialIndices)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Act
  std::vector<uint32_t> indices;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < 5; ++i) {
      auto payload = MakeTestBuffer(512 + i * 100, 0x01, 16, 32);
      indices.push_back(
        emitter.Emit(std::move(payload), "buf" + std::to_string(i)));
    }

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(indices.size(), 5);
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(indices[i], i);
  }
  EXPECT_EQ(emitter.Count(), 5);
  EXPECT_TRUE(success);
}

//! Verify emitting identical buffers returns the same index.
NOLINT_TEST_F(BufferEmitterTest, Emit_DuplicateBuffer_ReturnsSameIndex)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Act
  uint32_t idx0 = 0;
  uint32_t idx1 = 0;
  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto buf0 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xAB });
    auto buf1 = MakeTestBuffer(256, 0x01, 16, 32, std::byte { 0xAB });

    // Hashing is optional. Provide a non-zero stored hash to enable dedupe.
    buf0.content_hash = 0x1111222233334444ULL;
    buf1.content_hash = 0x1111222233334444ULL;

    idx0 = emitter.Emit(std::move(buf0), "dupe");
    idx1 = emitter.Emit(std::move(buf1), "dupe");
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert
  EXPECT_EQ(idx0, 0);
  EXPECT_EQ(idx1, 0);
  EXPECT_EQ(emitter.Count(), 1);

  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));
  ASSERT_EQ(table.size(), 1);
}

//! Verify index is returned immediately before I/O completes.
NOLINT_TEST_F(BufferEmitterTest, Emit_ReturnsImmediately_BeforeIOCompletes)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);
  auto payload = MakeTestBuffer(4 * 1024, 0x01, 16, 32);

  // Act
  uint32_t index = 0;
  bool had_pending = false;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    index = emitter.Emit(std::move(payload), "buf0");
    had_pending = emitter.PendingCount() > 0;

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(index, 0); // Index assigned immediately
  EXPECT_TRUE(had_pending); // I/O was queued
  EXPECT_TRUE(success);
}

//! Verify emitting after Finalize() is rejected.
NOLINT_TEST_F(BufferEmitterTest, Emit_AfterFinalize_Throws)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Act & Assert
  co::Run(*loop_, [&]() -> Co<> {
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW((void)emitter.Emit(MakeTestBuffer(256, 0x01, 16, 32), "buf0"),
      std::runtime_error);
  });
}

//=== PAK Format Compliance Tests
//===------------------------------------------//

//! Verify table file has correct packed size (32 bytes per entry).
NOLINT_TEST_F(BufferEmitterTest, Finalize_TableFileHasCorrectPackedSize)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);
  constexpr int kBufferCount = 3;

  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < kBufferCount; ++i) {
      auto idx = emitter.Emit(
        MakeTestBuffer(256, 0x01, 16, 32, static_cast<std::byte>(0xA0 + i)),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, static_cast<uint32_t>(i));
    }
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: Table file size = count * sizeof(BufferResourceDesc)
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  EXPECT_TRUE(std::filesystem::exists(table_path));

  const auto table_size = std::filesystem::file_size(table_path);
  EXPECT_EQ(table_size, kBufferCount * sizeof(PakBufferResourceDesc));
  EXPECT_EQ(table_size, kBufferCount * 32); // Explicit 32-byte check
}

//! Verify table entries have correctly aligned offsets based on buffer
//! alignment.
NOLINT_TEST_F(BufferEmitterTest, Finalize_TableEntriesHaveCorrectAlignedOffsets)
{
  // Arrange: Create buffers with different alignments
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

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
  co::Run(*loop_, [&]() -> Co<> {
    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign0, 32), "buf0");
    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign1, 32), "buf1");
    auto idx2 = emitter.Emit(MakeTestBuffer(kSize2, 0x02, kAlign2, 0), "buf2");
    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    EXPECT_EQ(idx2, 2);
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: Parse table and verify offsets
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 3);

  // Verify offsets follow alignment rules
  const auto expected_offset0 = 0ULL;
  const auto expected_offset1 = AlignUp(expected_offset0 + kSize0, kAlign1);
  const auto expected_offset2 = AlignUp(expected_offset1 + kSize1, kAlign2);

  EXPECT_EQ(table[0].data_offset, expected_offset0);
  EXPECT_EQ(table[0].size_bytes, kSize0);

  EXPECT_EQ(table[1].data_offset, expected_offset1);
  EXPECT_EQ(table[1].size_bytes, kSize1);

  EXPECT_EQ(table[2].data_offset, expected_offset2);
  EXPECT_EQ(table[2].size_bytes, kSize2);

  // Verify all offsets are properly aligned
  EXPECT_EQ(table[0].data_offset % kAlign0, 0);
  EXPECT_EQ(table[1].data_offset % kAlign1, 0);
  EXPECT_EQ(table[2].data_offset % kAlign2, 0);
}

//! Verify table entries preserve buffer metadata (usage, stride, format, hash).
NOLINT_TEST_F(BufferEmitterTest, Finalize_TableEntriesPreserveMetadata)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

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
  co::Run(*loop_, [&]() -> Co<> {
    auto idx0 = emitter.Emit(std::move(vertex_payload), "vb");
    auto idx1 = emitter.Emit(std::move(index_payload), "ib");
    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: Parse table and verify metadata
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 2);

  // Vertex buffer entry
  EXPECT_EQ(table[0].size_bytes, 512);
  EXPECT_EQ(table[0].usage_flags, 0x01);
  EXPECT_EQ(table[0].element_stride, 32);
  EXPECT_EQ(table[0].element_format, 0);
  EXPECT_EQ(table[0].content_hash, 0xDEADBEEFCAFEBABE);

  // Index buffer entry
  EXPECT_EQ(table[1].size_bytes, 256);
  EXPECT_EQ(table[1].usage_flags, 0x02);
  EXPECT_EQ(table[1].element_stride, 0);
  EXPECT_EQ(table[1].element_format, 0);
  EXPECT_EQ(table[1].content_hash, 0x1234567890ABCDEF);
}

//! Verify data file contains correct content at aligned offsets.
NOLINT_TEST_F(BufferEmitterTest, Finalize_DataFileContainsCorrectContent)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Create two buffers with distinct content
  CookedBufferPayload buf0;
  buf0.data.resize(100);
  std::fill(buf0.data.begin(), buf0.data.end(), std::byte { 0xAA });
  buf0.alignment = 16;
  buf0.usage_flags = 0x01;

  CookedBufferPayload buf1;
  buf1.data.resize(200);
  std::fill(buf1.data.begin(), buf1.data.end(), std::byte { 0xBB });
  buf1.alignment = 16;
  buf1.usage_flags = 0x01;

  co::Run(*loop_, [&]() -> Co<> {
    auto idx0 = emitter.Emit(std::move(buf0), "buf0");
    auto idx1 = emitter.Emit(std::move(buf1), "buf1");
    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    co_await emitter.Finalize();
  });

  // Assert: Read data file and verify content
  const auto data_path = test_dir_ / Layout().BuffersDataRelPath();
  const auto data_file = ReadBinaryFile(data_path);

  // Buffer 0 at offset 0
  ASSERT_GE(data_file.size(), 100);
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(data_file[i], std::byte { 0xAA })
      << "Buffer 0 content mismatch at byte " << i;
  }

  // Buffer 1 at aligned offset = AlignUp(100, 16) = 112
  constexpr auto kOffset1 = AlignUp(100, 16);
  ASSERT_GE(data_file.size(), kOffset1 + 200);
  for (size_t i = 0; i < 200; ++i) {
    EXPECT_EQ(data_file[kOffset1 + i], std::byte { 0xBB })
      << "Buffer 1 content mismatch at byte " << i;
  }

  // Verify padding between buffers is zeros
  for (size_t i = 100; i < kOffset1; ++i) {
    EXPECT_EQ(data_file[i], std::byte { 0x00 })
      << "Padding should be zeros at byte " << i;
  }
}

//! Verify data file size accounts for alignment padding.
NOLINT_TEST_F(BufferEmitterTest, Finalize_DataFileSizeIncludesPadding)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  constexpr size_t kSize0 = 100;
  constexpr size_t kSize1 = 200;
  constexpr uint64_t kAlign = 16;

  co::Run(*loop_, [&]() -> Co<> {
    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign, 32), "buf0");
    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign, 32), "buf1");
    EXPECT_EQ(idx0, 0);
    EXPECT_EQ(idx1, 1);
    co_await emitter.Finalize();
  });

  // Assert: Data file size = padding + data
  const auto data_path = test_dir_ / Layout().BuffersDataRelPath();
  const auto data_size = std::filesystem::file_size(data_path);

  // Expected: offset0=0, size0=100, offset1=AlignUp(100,16)=112, size1=200
  // Total = 112 + 200 = 312
  const auto expected_size = AlignUp(kSize0, kAlign) + kSize1;
  EXPECT_EQ(data_size, expected_size);
}

//=== Finalization Tests
//===---------------------------------------------------//

//! Verify finalization waits for pending I/O.
NOLINT_TEST_F(BufferEmitterTest, Finalize_WaitsForPendingIO)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);
  auto idx0 = emitter.Emit(MakeTestBuffer(2048, 0x01, 16, 32), "buf0");
  auto idx1 = emitter.Emit(MakeTestBuffer(1024, 0x02, 4, 0), "buf1");
  EXPECT_EQ(idx0, 0);
  EXPECT_EQ(idx1, 1);

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> { success = co_await emitter.Finalize(); });

  // Assert
  EXPECT_TRUE(success);
  EXPECT_EQ(emitter.PendingCount(), 0);
  EXPECT_EQ(emitter.ErrorCount(), 0);
}

//! Verify finalization with no buffers succeeds without writing files.
NOLINT_TEST_F(BufferEmitterTest, Finalize_NoBuffers_SucceedsWithoutWritingFiles)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> { success = co_await emitter.Finalize(); });

  // Assert
  EXPECT_TRUE(success);

  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  EXPECT_FALSE(std::filesystem::exists(table_path));
}

//=== State Query Tests
//===----------------------------------------------------//

//! Verify DataFileSize tracks accumulated data with alignment.
NOLINT_TEST_F(BufferEmitterTest, DataFileSize_TracksAccumulatedSize)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);
  constexpr size_t kSize0 = 100;
  constexpr size_t kSize1 = 200;
  constexpr uint64_t kAlign = 16;

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    // Assert initial state
    EXPECT_EQ(emitter.DataFileSize(), 0);

    auto idx0 = emitter.Emit(MakeTestBuffer(kSize0, 0x01, kAlign, 32), "buf0");
    EXPECT_EQ(idx0, 0);
    // First buffer: offset 0, size 100 -> file size 100
    EXPECT_EQ(emitter.DataFileSize(), kSize0);

    auto idx1 = emitter.Emit(MakeTestBuffer(kSize1, 0x01, kAlign, 32), "buf1");
    EXPECT_EQ(idx1, 1);
    // Second buffer: offset = AlignUp(100, 16) = 112, size 200 -> file size 312
    EXPECT_EQ(emitter.DataFileSize(), AlignUp(kSize0, kAlign) + kSize1);

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_TRUE(success);
}

//! Verify Count tracks number of emitted buffers.
NOLINT_TEST_F(BufferEmitterTest, Count_TracksEmittedBuffers)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Assert initial state
  EXPECT_EQ(emitter.Count(), 0);

  // Act & Assert
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (uint32_t i = 0; i < 10; ++i) {
      auto idx = emitter.Emit(
        MakeTestBuffer(64, 0x01, 16, 32, static_cast<std::byte>(0xC0 + i)),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, i);
      EXPECT_EQ(emitter.Count(), i + 1);
    }

    success = co_await emitter.Finalize();
  });

  EXPECT_EQ(emitter.Count(), 10);
  EXPECT_TRUE(success);
}

//=== Edge Cases ===----------------------------------------------------------//

//! Verify handling of zero-alignment (should default to 1 or minimum).
NOLINT_TEST_F(BufferEmitterTest, Emit_ZeroAlignment_UsesDefaultAlignment)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // Buffer with zero alignment should use default (16)
  auto payload = MakeTestBuffer(100, 0x01, 0, 32);

  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto idx = emitter.Emit(std::move(payload), "buf0");
    EXPECT_EQ(idx, 0);
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: Table entry exists
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 1);
  EXPECT_EQ(table[0].size_bytes, 100);
}

//! Verify large buffer emission.
NOLINT_TEST_F(BufferEmitterTest, Emit_LargeBuffer_SucceedsWithCorrectSize)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  // 1MB buffer
  constexpr size_t kLargeSize = 1024 * 1024;
  auto payload = MakeTestBuffer(kLargeSize, 0x01, 16, 32);

  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto idx = emitter.Emit(std::move(payload), "buf0");
    EXPECT_EQ(idx, 0);
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: Data file has correct size
  const auto data_path = test_dir_ / Layout().BuffersDataRelPath();
  EXPECT_EQ(std::filesystem::file_size(data_path), kLargeSize);

  // Table entry has correct size
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), 1);
  EXPECT_EQ(table[0].size_bytes, kLargeSize);
}

//! Verify many small buffers with alignment padding.
NOLINT_TEST_F(BufferEmitterTest, Emit_ManySmallBuffers_AllAlignedCorrectly)
{
  // Arrange
  BufferEmitter emitter(*writer_, BufferAggregator(), Layout(), test_dir_);

  constexpr int kBufferCount = 50;
  constexpr size_t kBufferSize = 17; // Intentionally not aligned
  constexpr uint64_t kAlignment = 16;

  bool tables_ok = false;
  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < kBufferCount; ++i) {
      auto idx = emitter.Emit(MakeTestBuffer(kBufferSize, 0x01, kAlignment, 32,
                                static_cast<std::byte>(0x10 + (i & 0x7F))),
        "buf" + std::to_string(i));
      EXPECT_EQ(idx, static_cast<uint32_t>(i));
    }
    co_await emitter.Finalize();
    tables_ok = co_await table_registry_->FinalizeAll();
    co_return;
  });

  EXPECT_TRUE(tables_ok);

  // Assert: All table entries have aligned offsets
  const auto table_path = test_dir_ / Layout().BuffersTableRelPath();
  const auto table = ParseBufferTable(ReadBinaryFile(table_path));

  ASSERT_EQ(table.size(), kBufferCount);

  uint64_t expected_offset = 0;
  for (int i = 0; i < kBufferCount; ++i) {
    EXPECT_EQ(table[i].data_offset, expected_offset)
      << "Buffer " << i << " has wrong offset";
    EXPECT_EQ(table[i].data_offset % kAlignment, 0)
      << "Buffer " << i << " offset not aligned";
    EXPECT_EQ(table[i].size_bytes, kBufferSize)
      << "Buffer " << i << " has wrong size";

    // Calculate next expected offset
    expected_offset = AlignUp(expected_offset + kBufferSize, kAlignment);
  }
}

} // namespace
