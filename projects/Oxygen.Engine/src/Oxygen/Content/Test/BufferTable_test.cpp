//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/BufferResource.h>

#include <Oxygen/Base/MemoryStream.h>
#include <Oxygen/Base/Writer.h>

using ::testing::Eq;

namespace {

//=== BufferResourceTable Basic Functionality Tests ===----------------------//

//! \brief BufferResourceTable_BasicLoadAndCache_CorrectBehavior
/*! Verifies that BufferResourceTable loads, caches, and unloads resources as
   expected. Scenario: Table is constructed from a fake stream with one
   BufferResourceDesc. The resource is loaded on demand, cached, and can be
   unloaded.
*/

class BufferTableBasicTest : public ::testing::Test {
protected:
  using BufferResource = oxygen::data::BufferResource;
  using BufferResourceDesc = oxygen::data::pak::BufferResourceDesc;
  using ResourceIndexT = oxygen::data::pak::ResourceIndexT;
  using MemoryStream = oxygen::serio::MemoryStream;
  using Writer = oxygen::serio::Writer<MemoryStream>;
  using BufferTable
    = oxygen::content::ResourceTable<BufferResource, MemoryStream>;

  BufferResourceDesc desc;
  std::vector<std::byte> io_buffer;
  std::unique_ptr<BufferTable> table;
  oxygen::data::pak::ResourceTable table_meta;

  void SetUp() override
  {
    // Place data immediately after header, before table descriptor
    constexpr size_t header_size = 8; // Simulated header size
    constexpr size_t data_size = 64;
    constexpr size_t table_offset = header_size + data_size;
    constexpr uint32_t count = 1;
    constexpr uint32_t entry_size = sizeof(BufferResourceDesc);

    desc = BufferResourceDesc {
      .data_offset = header_size,
      .size_bytes = data_size,
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer
        | BufferResource::UsageFlags::kCPUReadable),
      .element_stride = 1,
      .element_format = 0, // Format::kUnknown (raw buffer)
      .reserved = {},
    };

    // Buffer must be large enough for header, data, and table
    io_buffer.resize(table_offset + count * entry_size);

    // Write dummy header
    for (size_t i = 0; i < header_size; ++i)
      io_buffer[i] = std::byte { 0xEE };

    // Write dummy buffer data immediately after header
    for (size_t i = 0; i < data_size; ++i)
      io_buffer[header_size + i] = std::byte { 0xCD }; // Arbitrary pattern

    // Write the descriptor after data
    auto stream = std::make_unique<MemoryStream>(
      std::span<std::byte>(io_buffer.data(), io_buffer.size()));
    auto writer = std::make_unique<Writer>(*stream);
    (void)stream->seek(table_offset);
    ASSERT_TRUE(writer->write(desc));

    // Reset stream position and create the table
    (void)stream->seek(0);
    table_meta.offset = table_offset;
    table_meta.count = count;
    table_meta.entry_size = entry_size;
    table = std::make_unique<BufferTable>(std::move(stream), table_meta,
      oxygen::content::loaders::LoadBufferResource<MemoryStream>);
  }
};

//! Test: Table loads, caches, and unloads a BufferResource correctly.
NOLINT_TEST_F(BufferTableBasicTest, Smoke)
{
  // Arrange: (done in SetUp)

  // Act/Assert: Check initial state load resource, check cache, unload
  EXPECT_EQ(table->Size(), 1u);
  EXPECT_FALSE(table->HasResource(0));

  // Act/Assert: load resource, check cache, unload
  auto res = table->GetOrLoadResource(0);
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->GetDataOffset(), desc.data_offset);
  EXPECT_EQ(res->GetDataSize(), desc.size_bytes);
  EXPECT_EQ(static_cast<uint32_t>(res->GetUsageFlags()), desc.usage_flags);
  EXPECT_EQ(res->GetElementStride(), desc.element_stride);
  EXPECT_EQ(static_cast<uint8_t>(res->GetElementFormat()), desc.element_format);
  EXPECT_TRUE(res->IsRaw());

  // Assert: Check cache
  EXPECT_TRUE(table->HasResource(0));

  // Act/Assert: unload
  table->OnResourceUnloaded(0);
  EXPECT_FALSE(table->HasResource(0));
}

} // anonymous namespace
