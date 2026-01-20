//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
namespace data = oxygen::data;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

struct SceneStringTableBuilder final {
  std::vector<std::byte> bytes { std::byte { 0 } };

  auto Add(std::string_view text) -> data::pak::StringTableOffsetT
  {
    const auto offset
      = static_cast<data::pak::StringTableOffsetT>(bytes.size());
    for (const char c : text) {
      bytes.push_back(std::byte { static_cast<unsigned char>(c) });
    }
    bytes.push_back(std::byte { 0 });
    return offset;
  }
};

struct FakeSceneAdapter final {
  SceneBuild build;
  bool succeed = true;

  auto BuildSceneStage(const SceneStageInput& input,
    std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
  {
    static_cast<void>(input);
    static_cast<void>(diagnostics);
    return SceneStageResult { .build = build, .success = succeed };
  }
};

auto MakeMinimalSceneBuild(std::string_view name) -> SceneBuild
{
  SceneStringTableBuilder strings;
  const auto name_offset = strings.Add(name);

  SceneBuild build;
  build.nodes.push_back(data::pak::NodeRecord {
    .node_id = data::AssetKey { .guid = { 1 } },
    .scene_name_offset = name_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.strings = std::move(strings.bytes);
  return build;
}

auto ReadSceneDesc(const std::vector<std::byte>& bytes)
  -> data::pak::SceneAssetDesc
{
  data::pak::SceneAssetDesc desc {};
  if (bytes.size() < sizeof(desc)) {
    return desc;
  }
  std::memcpy(&desc, bytes.data(), sizeof(desc));
  return desc;
}

auto ReadNodeRecord(const std::vector<std::byte>& bytes,
  const data::pak::SceneAssetDesc& desc, size_t index) -> data::pak::NodeRecord
{
  data::pak::NodeRecord record {};
  const auto offset = desc.nodes.offset + index * sizeof(record);
  if (bytes.size() < offset + sizeof(record)) {
    return record;
  }
  std::memcpy(&record, bytes.data() + offset, sizeof(record));
  return record;
}

auto ReadEnvironmentHeader(const std::vector<std::byte>& bytes, size_t offset)
  -> data::pak::SceneEnvironmentBlockHeader
{
  data::pak::SceneEnvironmentBlockHeader header {};
  if (bytes.size() < offset + sizeof(header)) {
    return header;
  }
  std::memcpy(&header, bytes.data() + offset, sizeof(header));
  return header;
}

auto ReadComponentDirectory(
  const std::vector<std::byte>& bytes, const data::pak::SceneAssetDesc& desc)
  -> std::vector<data::pak::SceneComponentTableDesc>
{
  if (desc.component_table_count == 0) {
    return {};
  }

  const size_t dir_bytes = static_cast<size_t>(desc.component_table_count)
    * sizeof(data::pak::SceneComponentTableDesc);
  if (bytes.size() < desc.component_table_directory_offset + dir_bytes) {
    return {};
  }

  std::vector<data::pak::SceneComponentTableDesc> entries;
  entries.resize(desc.component_table_count);
  std::memcpy(entries.data(),
    bytes.data() + desc.component_table_directory_offset, dir_bytes);
  return entries;
}

auto ReadRenderableRecord(const std::vector<std::byte>& bytes,
  const data::pak::SceneComponentTableDesc& entry, size_t index)
  -> data::pak::RenderableRecord
{
  data::pak::RenderableRecord record {};
  const auto offset = entry.table.offset + index * sizeof(record);
  if (bytes.size() < offset + sizeof(record)) {
    return record;
  }
  std::memcpy(&record, bytes.data() + offset, sizeof(record));
  return record;
}

class ScenePipelineTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify a minimal scene produces a single node and empty environment.
NOLINT_TEST_F(ScenePipelineTest, Collect_MinimalScene_BuildsDescriptor)
{
  // Arrange
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    ScenePipeline pipeline(pool);

    auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
      "Scene", {}, {},
      ImportRequest {
        .source_path = "TestScene.scene",
      },
      {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  const auto& bytes = result.cooked->descriptor_bytes;
  const auto desc = ReadSceneDesc(bytes);
  EXPECT_EQ(desc.nodes.count, 1u);
  EXPECT_EQ(desc.component_table_count, 0u);

  const auto node = ReadNodeRecord(bytes, desc, 0);
  EXPECT_EQ(node.parent_index, 0u);
  EXPECT_NE(node.scene_name_offset, 0u);

  const auto env_header_offset
    = bytes.size() - sizeof(data::pak::SceneEnvironmentBlockHeader);
  const auto env_header = ReadEnvironmentHeader(bytes, env_header_offset);
  EXPECT_EQ(env_header.systems_count, 0u);
  EXPECT_EQ(
    env_header.byte_size, sizeof(data::pak::SceneEnvironmentBlockHeader));
}

//! Verify renderable component tables are sorted by node index.
NOLINT_TEST_F(ScenePipelineTest, Collect_SortsRenderables_ByNodeIndex)
{
  // Arrange
  SceneStringTableBuilder strings;
  const auto root_offset = strings.Add("Root");
  const auto child_offset = strings.Add("Child");

  SceneBuild build;
  build.nodes.push_back(data::pak::NodeRecord {
    .node_id = data::AssetKey { .guid = { 1 } },
    .scene_name_offset = root_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.nodes.push_back(data::pak::NodeRecord {
    .node_id = data::AssetKey { .guid = { 2 } },
    .scene_name_offset = child_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.strings = std::move(strings.bytes);
  build.renderables = {
    data::pak::RenderableRecord {
      .node_index = 1,
      .geometry_key = data::AssetKey { .guid = { 42 } },
      .visible = 1,
    },
    data::pak::RenderableRecord {
      .node_index = 0,
      .geometry_key = data::AssetKey { .guid = { 43 } },
      .visible = 1,
    },
  };

  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = std::move(build);

  ScenePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    ScenePipeline pipeline(pool);

    auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
      "Scene", {}, {},
      ImportRequest {
        .source_path = "TestScene.scene",
      },
      {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  const auto& bytes = result.cooked->descriptor_bytes;
  const auto desc = ReadSceneDesc(bytes);
  EXPECT_EQ(desc.component_table_count, 1u);
  EXPECT_EQ(desc.component_table_directory_offset,
    desc.scene_strings.offset + desc.scene_strings.size);

  const auto entries = ReadComponentDirectory(bytes, desc);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].component_type,
    static_cast<uint32_t>(data::ComponentType::kRenderable));
  EXPECT_EQ(entries[0].table.entry_size, sizeof(data::pak::RenderableRecord));
  EXPECT_EQ(entries[0].table.count, 2u);

  const auto renderable0 = ReadRenderableRecord(bytes, entries[0], 0);
  const auto renderable1 = ReadRenderableRecord(bytes, entries[0], 1);
  EXPECT_LT(renderable0.node_index, renderable1.node_index);
}

//! Verify environment block records are appended to the descriptor.
NOLINT_TEST_F(ScenePipelineTest, Collect_WithEnvironmentBlock_AppendsBlock)
{
  // Arrange
  data::pak::FogEnvironmentRecord fog {};
  fog.density = 0.05F;
  const auto fog_bytes = std::as_bytes(
    std::span<const data::pak::FogEnvironmentRecord, 1>(&fog, 1));

  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    ScenePipeline pipeline(pool);

    auto item
      = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter), "Scene", {},
        {
          SceneEnvironmentSystem {
            .system_type
            = static_cast<uint32_t>(data::pak::EnvironmentComponentType::kFog),
            .record_bytes
            = std::vector<std::byte>(fog_bytes.begin(), fog_bytes.end()),
          },
        },
        ImportRequest {
          .source_path = "TestScene.scene",
        },
        {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());

  const auto& bytes = result.cooked->descriptor_bytes;
  const auto header_size = sizeof(data::pak::SceneEnvironmentBlockHeader);
  const auto record_size = sizeof(data::pak::FogEnvironmentRecord);
  const auto env_header_offset = bytes.size() - header_size - record_size;
  const auto env_header = ReadEnvironmentHeader(bytes, env_header_offset);
  EXPECT_EQ(env_header.systems_count, 1u);
  EXPECT_EQ(env_header.byte_size, header_size + record_size);
}

} // namespace
