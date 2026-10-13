//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/ScenePipeline.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SceneBuildTestSupport.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
namespace data = oxygen::data;
using oxygen::cooker::test::FakeSceneAdapter;
using oxygen::cooker::test::MakeFirstByteAssetKey;
using oxygen::cooker::test::MakeMinimalSceneBuild;
using oxygen::cooker::test::SceneStringTableBuilder;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

auto ReadSceneDesc(const std::vector<std::byte>& bytes)
  -> data::pak::world::SceneAssetDesc
{
  data::pak::world::SceneAssetDesc desc {};
  if (bytes.size() < sizeof(desc)) {
    return desc;
  }
  std::memcpy(&desc, bytes.data(), sizeof(desc));
  return desc;
}

auto ReadNodeRecord(const std::vector<std::byte>& bytes,
  const data::pak::world::SceneAssetDesc& desc, size_t index)
  -> data::pak::world::NodeRecord
{
  data::pak::world::NodeRecord record {};
  const auto offset = desc.nodes.offset + (index * sizeof(record));
  if (bytes.size() < offset + sizeof(record)) {
    return record;
  }
  std::memcpy(&record, bytes.data() + offset, sizeof(record));
  return record;
}

auto ReadEnvironmentHeader(const std::vector<std::byte>& bytes, size_t offset)
  -> data::pak::world::SceneEnvironmentBlockHeader
{
  data::pak::world::SceneEnvironmentBlockHeader header {};
  if (bytes.size() < offset + sizeof(header)) {
    return header;
  }
  std::memcpy(&header, bytes.data() + offset, sizeof(header));
  return header;
}

auto ReadComponentDirectory(const std::vector<std::byte>& bytes,
  const data::pak::world::SceneAssetDesc& desc)
  -> std::vector<data::pak::world::SceneComponentTableDesc>
{
  if (desc.component_table_count == 0) {
    return {};
  }

  const size_t dir_bytes = static_cast<size_t>(desc.component_table_count)
    * sizeof(data::pak::world::SceneComponentTableDesc);
  if (bytes.size() < desc.component_table_directory_offset + dir_bytes) {
    return {};
  }

  std::vector<data::pak::world::SceneComponentTableDesc> entries;
  entries.resize(desc.component_table_count);
  std::memcpy(entries.data(),
    bytes.data() + desc.component_table_directory_offset, dir_bytes);
  return entries;
}

auto ReadRenderableRecord(const std::vector<std::byte>& bytes,
  const data::pak::world::SceneComponentTableDesc& entry, size_t index)
  -> data::pak::world::RenderableRecord
{
  data::pak::world::RenderableRecord record {};
  const auto offset = entry.table.offset + (index * sizeof(record));
  if (bytes.size() < offset + sizeof(record)) {
    return record;
  }
  std::memcpy(&record, bytes.data() + offset, sizeof(record));
  return record;
}

class ScenePipelineTest : public testing::Test {
protected:
  ImportEventLoop loop_;
  ThreadPool pool_ { loop_, 1 };
};

NOLINT_TEST_F(ScenePipelineTest, RejectsUnsupportedNodeFlagSourceModes)
{
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");
  adapter->build.nodes.front().inherited_flags
    = data::pak::world::kSceneNodeFlag_Static;
  ScenePipeline::WorkResult result;
  co::Run(loop_, [&] -> Co<> {
    ScenePipeline pipeline(pool_);
    NamingService naming_service(NamingService::Config {
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = false,
      .enforce_uniqueness = false,
    });
    auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
      "Scene", {}, {}, ImportRequest { .source_path = "Flags.scene" },
      oxygen::observer_ptr { &naming_service }, {});
    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(std::ranges::any_of(
    result.diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
      return diagnostic.code == "scene.node.flags_invalid";
    }));
}

NOLINT_TEST_F(ScenePipelineTest, CollectMinimalSceneBuildsDescriptor)
{
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;

  co::Run(loop_, [&] -> Co<> {
    ScenePipeline pipeline(pool_);

    NamingService naming_service(NamingService::Config {
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = false,
      .enforce_uniqueness = false,
    });

    auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
      "Scene", {}, {},
      ImportRequest {
        .source_path = "TestScene.scene",
      },
      oxygen::observer_ptr { &naming_service }, {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  ASSERT_TRUE(result.success);
  ASSERT_HAS_VALUE(result.cooked)
    << "Expected result.cooked to contain a value";
  const auto& bytes = result.cooked->descriptor_bytes;
  const auto desc = ReadSceneDesc(bytes);
  EXPECT_EQ(desc.nodes.count, 1U);
  EXPECT_EQ(desc.component_table_count, 0U);

  const auto node = ReadNodeRecord(bytes, desc, 0);
  EXPECT_EQ(node.parent_index, 0U);
  EXPECT_NE(node.scene_name_offset, 0U);

  const auto env_header_offset
    = bytes.size() - sizeof(data::pak::world::SceneEnvironmentBlockHeader);
  const auto env_header = ReadEnvironmentHeader(bytes, env_header_offset);
  EXPECT_EQ(env_header.systems_count, 0U);
  EXPECT_EQ(env_header.byte_size,
    sizeof(data::pak::world::SceneEnvironmentBlockHeader));
}

NOLINT_TEST_F(ScenePipelineTest, CollectSortsRenderablesByNodeIndex)
{
  SceneStringTableBuilder strings;
  const auto root_offset = strings.Add("Root");
  const auto child_offset = strings.Add("Child");

  SceneBuild build;
  build.nodes.push_back(data::pak::world::NodeRecord {
    .node_id = MakeFirstByteAssetKey(1U),
    .scene_name_offset = root_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.nodes.push_back(data::pak::world::NodeRecord {
    .node_id = MakeFirstByteAssetKey(2U),
    .scene_name_offset = child_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.strings = std::move(strings.bytes);
  build.renderables = {
    data::pak::world::RenderableRecord {
      .node_index = 1,
      .geometry_key = MakeFirstByteAssetKey(42U),
      .visible = 1,
    },
    data::pak::world::RenderableRecord {
      .node_index = 0,
      .geometry_key = MakeFirstByteAssetKey(43U),
      .visible = 1,
    },
  };

  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = std::move(build);

  ScenePipeline::WorkResult result;

  co::Run(loop_, [&] -> Co<> {
    ScenePipeline pipeline(pool_);

    NamingService naming_service(NamingService::Config {
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = false,
      .enforce_uniqueness = false,
    });

    auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
      "Scene", {}, {},
      ImportRequest {
        .source_path = "TestScene.scene",
      },
      oxygen::observer_ptr { &naming_service }, {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  ASSERT_TRUE(result.success);
  ASSERT_HAS_VALUE(result.cooked)
    << "Expected result.cooked to contain a value";
  const auto& bytes = result.cooked->descriptor_bytes;
  const auto desc = ReadSceneDesc(bytes);
  EXPECT_EQ(desc.component_table_count, 1U);
  EXPECT_EQ(desc.component_table_directory_offset,
    desc.scene_strings.offset + desc.scene_strings.size);

  const auto entries = ReadComponentDirectory(bytes, desc);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries.at(0).component_type,
    static_cast<uint32_t>(data::ComponentType::kRenderable));
  EXPECT_EQ(
    entries.at(0).table.entry_size, sizeof(data::pak::world::RenderableRecord));
  EXPECT_EQ(entries.at(0).table.count, 2U);

  const auto renderable0 = ReadRenderableRecord(bytes, entries.at(0), 0);
  const auto renderable1 = ReadRenderableRecord(bytes, entries.at(0), 1);
  EXPECT_LT(renderable0.node_index, renderable1.node_index);
  EXPECT_EQ(renderable0.geometry_key, MakeFirstByteAssetKey(43U));
  EXPECT_EQ(renderable1.geometry_key, MakeFirstByteAssetKey(42U));
}

NOLINT_TEST_F(ScenePipelineTest, CollectWithEnvironmentBlockAppendsBlock)
{
  data::pak::world::FogEnvironmentRecord fog {};
  fog.extinction_sigma_t_per_m = 0.05F;
  const auto fog_bytes = std::as_bytes(
    std::span<const data::pak::world::FogEnvironmentRecord, 1>(&fog, 1));

  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;

  co::Run(loop_, [&] -> Co<> {
    ScenePipeline pipeline(pool_);

    NamingService naming_service(NamingService::Config {
      .strategy = std::make_shared<NoOpNamingStrategy>(),
      .enable_namespacing = false,
      .enforce_uniqueness = false,
    });

    auto item
      = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter), "Scene", {},
        {
          SceneEnvironmentSystem {
            .system_type = static_cast<uint32_t>(
              data::pak::world::EnvironmentComponentType::kFog),
            .record_bytes
            = std::vector<std::byte>(fog_bytes.begin(), fog_bytes.end()),
          },
        },
        ImportRequest {
          .source_path = "TestScene.scene",
        },
        oxygen::observer_ptr { &naming_service }, {});

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      pipeline.Close();
      result = co_await pipeline.Collect();
      co_return kJoin;
    };
  });

  ASSERT_TRUE(result.success);
  ASSERT_HAS_VALUE(result.cooked)
    << "Expected result.cooked to contain a value";

  const auto& bytes = result.cooked->descriptor_bytes;
  const auto header_size
    = sizeof(data::pak::world::SceneEnvironmentBlockHeader);
  const auto record_size = sizeof(data::pak::world::FogEnvironmentRecord);
  const auto env_header_offset = bytes.size() - header_size - record_size;
  const auto env_header = ReadEnvironmentHeader(bytes, env_header_offset);
  EXPECT_EQ(env_header.systems_count, 1U);
  EXPECT_EQ(env_header.byte_size, header_size + record_size);
}

} // namespace
