//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

#include "Mocks/MockStream.h"

using oxygen::content::loaders::LoadSceneAsset;

namespace {

class SceneLoaderTest : public testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Reader = oxygen::serio::Reader<MockStream>;
  using Writer = oxygen::serio::Writer<MockStream>;

  SceneLoaderTest()
    : writer_(stream_)
    , reader_(stream_)
  {
  }

  auto MakeContextParseOnly() -> oxygen::content::LoaderContext
  {
    if (!stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek stream");
    }
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &reader_,
      .work_offline = true,
      .parse_only = true,
    };
  }

  auto MakeContextDecode() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    if (!stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek stream");
    }

    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();

    return { oxygen::content::LoaderContext {
               .current_asset_key = oxygen::data::AssetKey {},
               .source_token = oxygen::content::internal::SourceToken(1U),
               .desc_reader = &reader_,
               .work_offline = true,
               .dependency_collector = collector,
               .source_pak = nullptr,
               .parse_only = false,
             },
      collector };
  }

  auto WriteEmptyEnvironmentBlock() -> void
  {
    using oxygen::data::pak::world::SceneEnvironmentBlockHeader;

    SceneEnvironmentBlockHeader header {};
    header.byte_size = sizeof(SceneEnvironmentBlockHeader);
    header.systems_count = 0;

    const auto header_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&header), sizeof(header)));
    ASSERT_TRUE(header_write) << header_write.error().message();
  }

  auto WriteMinimalSceneWithRenderable(
    const oxygen::data::AssetKey geometry_key) -> void
  {
    using oxygen::data::pak::world::NodeRecord;
    using oxygen::data::pak::world::RenderableRecord;
    using oxygen::data::pak::world::SceneAssetDesc;

    SceneAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
    desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

    // Layout:
    // [SceneAssetDesc][NodeRecord x1][StringTable "\0root\0"][Directory
    // x1][Renderable x1]
    const uint32_t offset_nodes = static_cast<uint32_t>(sizeof(SceneAssetDesc));
    const uint32_t nodes_bytes = static_cast<uint32_t>(sizeof(NodeRecord));

    const std::array<std::byte, 6> strings
      = { std::byte { 0 }, std::byte { 'r' }, std::byte { 'o' },
          std::byte { 'o' }, std::byte { 't' }, std::byte { 0 } };
    const uint32_t offset_strings = offset_nodes + nodes_bytes;
    const uint32_t strings_bytes = static_cast<uint32_t>(strings.size());

    const uint32_t offset_directory = offset_strings + strings_bytes;
    const uint32_t dir_bytes = static_cast<uint32_t>(
      sizeof(oxygen::data::pak::world::SceneComponentTableDesc));

    const uint32_t offset_renderables = offset_directory + dir_bytes;

    desc.nodes.offset = offset_nodes;
    desc.nodes.count = 1;
    desc.nodes.entry_size = sizeof(NodeRecord);

    desc.scene_strings.offset = offset_strings;
    desc.scene_strings.size = strings_bytes;

    desc.component_table_directory_offset = offset_directory;
    desc.component_table_count = 1;

    // Write desc as raw bytes (packed, no floats).
    auto desc_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&desc), sizeof(desc)));
    ASSERT_TRUE(desc_write) << desc_write.error().message();

    NodeRecord node {};
    node.scene_name_offset = 1; // "root"
    node.parent_index = 0;

    auto node_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&node), sizeof(node)));
    ASSERT_TRUE(node_write) << node_write.error().message();

    auto strings_write = writer_.WriteBlob(strings);
    ASSERT_TRUE(strings_write) << strings_write.error().message();

    oxygen::data::pak::world::SceneComponentTableDesc table_desc {};
    table_desc.component_type
      = static_cast<uint32_t>(oxygen::data::ComponentType::kRenderable);
    table_desc.table.offset = offset_renderables;
    table_desc.table.count = 1;
    table_desc.table.entry_size = sizeof(RenderableRecord);

    auto dir_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&table_desc), sizeof(table_desc)));
    ASSERT_TRUE(dir_write) << dir_write.error().message();

    RenderableRecord renderable {};
    renderable.node_index = 0;
    renderable.geometry_key = geometry_key;

    auto rend_write = writer_.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&renderable), sizeof(renderable)));
    ASSERT_TRUE(rend_write) << rend_write.error().message();

    WriteEmptyEnvironmentBlock();

    auto flush_res = writer_.Flush();
    ASSERT_TRUE(flush_res) << flush_res.error().message();
  }

  MockStream stream_;
  Writer writer_;
  Reader reader_;
};

NOLINT_TEST_F(SceneLoaderTest, LoadSceneParseOnlySucceeds)
{
  WriteMinimalSceneWithRenderable(oxygen::data::AssetKey {});

  auto asset = LoadSceneAsset(MakeContextParseOnly());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetNodes().size(), 1U);
  EXPECT_EQ(asset->GetNodeName(asset->GetRootNode()), "root");
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneDecodeCollectsGeometryDependencies)
{
  auto geom_bytes
    = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  geom_bytes[0] = 0xABU;
  geom_bytes[1] = 0xCDU;
  const auto geom = oxygen::data::AssetKey::FromBytes(geom_bytes);

  WriteMinimalSceneWithRenderable(geom);

  auto [context, collector] = MakeContextDecode();
  auto asset = LoadSceneAsset(context);
  ASSERT_NE(asset, nullptr);

  ASSERT_THAT(collector->AssetDependencies(), ::testing::SizeIs(1));
  EXPECT_EQ(collector->AssetDependencies().front(), geom);
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneInputContextBindingNodeOutOfRangeThrows)
{
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputContextBindingRecord;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneComponentTableDesc;

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const uint32_t offset_nodes = static_cast<uint32_t>(sizeof(SceneAssetDesc));
  const uint32_t offset_strings = offset_nodes + sizeof(NodeRecord);
  const uint32_t strings_size = 6; // "\0root\0"
  const uint32_t offset_directory = offset_strings + strings_size;
  const uint32_t offset_table
    = offset_directory + sizeof(SceneComponentTableDesc);

  desc.nodes.offset = offset_nodes;
  desc.nodes.count = 1;
  desc.nodes.entry_size = sizeof(NodeRecord);
  desc.scene_strings.offset = offset_strings;
  desc.scene_strings.size = strings_size;
  desc.component_table_directory_offset = offset_directory;
  desc.component_table_count = 1;

  auto desc_write = writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&desc), sizeof(desc)));
  ASSERT_TRUE(desc_write) << desc_write.error().message();

  NodeRecord node {};
  node.scene_name_offset = 1;
  node.parent_index = 0;
  auto node_write = writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&node), sizeof(node)));
  ASSERT_TRUE(node_write) << node_write.error().message();

  const std::array<std::byte, 6> strings = { std::byte { 0 }, std::byte { 'r' },
    std::byte { 'o' }, std::byte { 'o' }, std::byte { 't' }, std::byte { 0 } };
  ASSERT_TRUE(writer_.WriteBlob(strings));

  SceneComponentTableDesc table_desc {};
  table_desc.component_type
    = static_cast<uint32_t>(oxygen::data::ComponentType::kInputContextBinding);
  table_desc.table.offset = offset_table;
  table_desc.table.count = 1;
  table_desc.table.entry_size = sizeof(InputContextBindingRecord);
  auto dir_write = writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&table_desc), sizeof(table_desc)));
  ASSERT_TRUE(dir_write) << dir_write.error().message();

  InputContextBindingRecord binding {};
  binding.node_index = 2; // out of range (only one node)
  binding.context_asset_key = AssetKey {};
  auto table_write = writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&binding), sizeof(binding)));
  ASSERT_TRUE(table_write) << table_write.error().message();
  WriteEmptyEnvironmentBlock();
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadSceneAsset(MakeContextParseOnly()); }, std::runtime_error);
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneInputContextBindingBadEntrySizeThrows)
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneComponentTableDesc;

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const uint32_t offset_nodes = static_cast<uint32_t>(sizeof(SceneAssetDesc));
  const uint32_t offset_strings = offset_nodes + sizeof(NodeRecord);
  const uint32_t strings_size = 6;
  const uint32_t offset_directory = offset_strings + strings_size;
  const uint32_t offset_table
    = offset_directory + sizeof(SceneComponentTableDesc);

  desc.nodes.offset = offset_nodes;
  desc.nodes.count = 1;
  desc.nodes.entry_size = sizeof(NodeRecord);
  desc.scene_strings.offset = offset_strings;
  desc.scene_strings.size = strings_size;
  desc.component_table_directory_offset = offset_directory;
  desc.component_table_count = 1;

  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&desc), sizeof(desc))));

  NodeRecord node {};
  node.scene_name_offset = 1;
  node.parent_index = 0;
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&node), sizeof(node))));

  const std::array<std::byte, 6> strings = { std::byte { 0 }, std::byte { 'r' },
    std::byte { 'o' }, std::byte { 'o' }, std::byte { 't' }, std::byte { 0 } };
  ASSERT_TRUE(writer_.WriteBlob(strings));

  SceneComponentTableDesc table_desc {};
  table_desc.component_type
    = static_cast<uint32_t>(oxygen::data::ComponentType::kInputContextBinding);
  table_desc.table.offset = offset_table;
  table_desc.table.count = 1;
  table_desc.table.entry_size = 16; // invalid
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&table_desc), sizeof(table_desc))));

  const std::array<std::byte, 16> bad_entry {};
  ASSERT_TRUE(writer_.WriteBlob(bad_entry));
  WriteEmptyEnvironmentBlock();
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadSceneAsset(MakeContextParseOnly()); }, std::runtime_error);
}

NOLINT_TEST_F(SceneLoaderTest, LoadSceneInputContextBindingUnsortedByNodeThrows)
{
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputContextBindingRecord;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneComponentTableDesc;

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const uint32_t offset_nodes = static_cast<uint32_t>(sizeof(SceneAssetDesc));
  const uint32_t nodes_size = 2U * sizeof(NodeRecord);
  const uint32_t offset_strings = offset_nodes + nodes_size;
  const uint32_t strings_size = 8; // "\0root\0n1\0"
  const uint32_t offset_directory = offset_strings + strings_size;
  const uint32_t offset_table
    = offset_directory + sizeof(SceneComponentTableDesc);

  desc.nodes.offset = offset_nodes;
  desc.nodes.count = 2;
  desc.nodes.entry_size = sizeof(NodeRecord);
  desc.scene_strings.offset = offset_strings;
  desc.scene_strings.size = strings_size;
  desc.component_table_directory_offset = offset_directory;
  desc.component_table_count = 1;

  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&desc), sizeof(desc))));

  NodeRecord node0 {};
  node0.scene_name_offset = 1;
  node0.parent_index = 0;
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&node0), sizeof(node0))));
  NodeRecord node1 {};
  node1.scene_name_offset = 6;
  node1.parent_index = 0;
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&node1), sizeof(node1))));

  const std::array<std::byte, 8> strings = { std::byte { 0 }, std::byte { 'r' },
    std::byte { 'o' }, std::byte { 'o' }, std::byte { 't' }, std::byte { 0 },
    std::byte { 'n' }, std::byte { 0 } };
  ASSERT_TRUE(writer_.WriteBlob(strings));

  SceneComponentTableDesc table_desc {};
  table_desc.component_type
    = static_cast<uint32_t>(oxygen::data::ComponentType::kInputContextBinding);
  table_desc.table.offset = offset_table;
  table_desc.table.count = 2;
  table_desc.table.entry_size = sizeof(InputContextBindingRecord);
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&table_desc), sizeof(table_desc))));

  InputContextBindingRecord a {};
  a.node_index = 1;
  a.context_asset_key = AssetKey {};
  InputContextBindingRecord b {};
  b.node_index = 0; // unsorted
  b.context_asset_key = AssetKey {};
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&a), sizeof(a))));
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(&b), sizeof(b))));
  WriteEmptyEnvironmentBlock();
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadSceneAsset(MakeContextParseOnly()); }, std::runtime_error);
}

} // namespace
