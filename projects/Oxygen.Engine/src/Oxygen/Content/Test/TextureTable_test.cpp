//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/TextureResource.h>

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

class TextureTableBasicTest : public ::testing::Test {
protected:
  using TextureResource = oxygen::data::TextureResource;
  using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;
  using ResourceIndexT = oxygen::data::pak::ResourceIndexT;
  using MemoryStream = oxygen::serio::MemoryStream;
  using Writer = oxygen::serio::Writer<MemoryStream>;
  using TextureTable
    = oxygen::content::ResourceTable<TextureResource, MemoryStream>;

  TextureResourceDesc desc;
  std::vector<std::byte> io_buffer;
  std::unique_ptr<TextureTable> table;
  oxygen::data::pak::ResourceTable table_meta;

  void SetUp() override
  {
    // Place data immediately after header, before table descriptor
    constexpr size_t header_size = 8; // Simulated header size
    constexpr size_t data_size = 128;
    constexpr size_t table_offset = header_size + data_size;
    constexpr uint32_t count = 1;
    constexpr uint32_t entry_size = sizeof(TextureResourceDesc);

    desc = TextureResourceDesc {
      .data_offset = header_size,
      .data_size = data_size,
      .texture_type = 2,
      .compression_type = 1,
      .width = 256,
      .height = 128,
      .depth = 1,
      .array_layers = 1,
      .mip_levels = 1,
      .format = static_cast<uint8_t>(oxygen::Format::kRGBA32Float),
      .alignment = 16,
      .is_cubemap = false,
    };

    // Buffer must be large enough for header, data, and table
    io_buffer.resize(table_offset + count * entry_size);

    // Write dummy header
    for (size_t i = 0; i < header_size; ++i)
      io_buffer[i] = std::byte { 0xFF };

    // Write dummy texture data immediately after header
    for (size_t i = 0; i < data_size; ++i) {
      io_buffer[header_size + i] = std::byte { 0xAB }; // Arbitrary pattern
    }

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
    table = std::make_unique<
      oxygen::content::ResourceTable<TextureResource, MemoryStream>>(
      std::move(stream), table_meta,
      oxygen::content::loaders::LoadTextureResource<MemoryStream>);
  }
};

//! Test: Table loads, caches, and unloads a TextureResource correctly.
NOLINT_TEST_F(TextureTableBasicTest, Smoke)
{
  // Arrange: (done in SetUp)

  // Act/Assert: Check initial state load resource, check cache, unload
  EXPECT_EQ(table->Size(), 1u);
  EXPECT_FALSE(table->HasResource(0));

  // Act/Assert: load resource, check cache, unload
  auto res = table->GetOrLoadResource(0);
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->GetDataOffset(), desc.data_offset);
  EXPECT_EQ(res->GetDataSize(), desc.data_size);
  EXPECT_EQ(res->GetWidth(), desc.width);
  EXPECT_EQ(res->GetHeight(), desc.height);
  EXPECT_EQ(
    static_cast<std::underlying_type_t<oxygen::Format>>(res->GetFormat()),
    desc.format);
  EXPECT_EQ(res->GetDataAlignment(), desc.alignment);
  EXPECT_EQ(res->IsCubemap(), desc.is_cubemap);

  // Assert: Check cache
  EXPECT_TRUE(table->HasResource(0));

  // Act/Assert: unload
  table->OnResourceUnloaded(0);
  EXPECT_FALSE(table->HasResource(0));
}

} // anonymous namespace
