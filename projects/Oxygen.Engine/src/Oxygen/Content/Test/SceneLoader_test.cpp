//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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

  auto WriteMinimalSceneWithRenderable(
    const oxygen::data::AssetKey geometry_key) -> void
  {
    using oxygen::data::pak::NodeRecord;
    using oxygen::data::pak::RenderableRecord;
    using oxygen::data::pak::SceneAssetDesc;

    SceneAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
    desc.header.version = 1;

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
      sizeof(oxygen::data::pak::SceneComponentTableDesc));

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

    oxygen::data::pak::SceneComponentTableDesc table_desc {};
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

    auto flush_res = writer_.Flush();
    ASSERT_TRUE(flush_res) << flush_res.error().message();
  }

  MockStream stream_;
  Writer writer_;
  Reader reader_;
};

NOLINT_TEST_F(SceneLoaderTest, LoadScene_ParseOnly_Succeeds)
{
  WriteMinimalSceneWithRenderable(oxygen::data::AssetKey {});

  auto asset = LoadSceneAsset(MakeContextParseOnly());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetNodes().size(), 1U);
  EXPECT_EQ(asset->GetNodeName(asset->GetRootNode()), "root");
}

NOLINT_TEST_F(SceneLoaderTest, LoadScene_Decode_CollectsGeometryDependencies)
{
  oxygen::data::AssetKey geom {};
  geom.guid[0] = 0xAB;
  geom.guid[1] = 0xCD;

  WriteMinimalSceneWithRenderable(geom);

  auto [context, collector] = MakeContextDecode();
  auto asset = LoadSceneAsset(context);
  ASSERT_NE(asset, nullptr);

  ASSERT_THAT(collector->AssetDependencies(), ::testing::SizeIs(1));
  EXPECT_EQ(collector->AssetDependencies().front(), geom);
}

} // namespace
