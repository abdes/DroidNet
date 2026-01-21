//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
namespace data = oxygen::data;

namespace {

//=== Helpers
//===--------------------------------------------------------------//

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

auto MakeBufferPayload() -> CookedBufferPayload
{
  CookedBufferPayload cooked;
  cooked.data = { std::byte { 0x01 }, std::byte { 0x02 } };
  cooked.alignment = 16;
  cooked.usage_flags = 1;
  cooked.element_stride = 4;
  cooked.element_format = 0;
  cooked.content_hash = 0;
  return cooked;
}

auto MakeTextureWorkItem() -> TexturePipeline::WorkItem
{
  ScratchImage image = ScratchImage::Create(ScratchImageMeta {
    .texture_type = oxygen::TextureType::kTexture2D,
    .width = 1,
    .height = 1,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = oxygen::Format::kRGBA8UNorm,
  });

  TextureImportDesc desc {};
  desc.texture_type = oxygen::TextureType::kTexture2D;
  desc.width = 1;
  desc.height = 1;
  desc.depth = 1;
  desc.array_layers = 1;
  desc.mip_policy = MipPolicy::kMaxCount;
  desc.max_mip_levels = 1;
  desc.intent = TextureIntent::kAlbedo;
  desc.output_format = oxygen::Format::kRGBA8UNorm;
  desc.bc7_quality = Bc7Quality::kNone;

  return TexturePipeline::WorkItem {
    .source_id = "tex0",
    .texture_id = "tex0",
    .source_key = nullptr,
    .desc = desc,
    .packing_policy_id = "d3d12",
    .output_format_is_override = true,
    .failure_policy = TexturePipeline::FailurePolicy::kStrict,
    .equirect_to_cubemap = false,
    .cubemap_face_size = 0,
    .cubemap_layout = CubeMapImageLayout::kUnknown,
    .source = std::move(image),
    .stop_token = {},
  };
}

auto MakeMaterialWorkItem() -> MaterialPipeline::WorkItem
{
  MaterialPipeline::WorkItem item;
  item.source_id = "mat0";
  item.material_name = "Material_0";
  item.storage_material_name = "Material_0";
  item.shader_requests = {
    ShaderRequest {
      .shader_type = 1,
      .source_path = "Passes/Forward/ForwardMesh_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
      .shader_hash = 0,
    },
    ShaderRequest {
      .shader_type = 2,
      .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
      .entry_point = "PS",
      .defines = {},
      .shader_hash = 0,
    },
  };
  item.request.source_path = "Material.gltf";
  return item;
}

auto MakeGeometryWorkItem() -> MeshBuildPipeline::WorkItem
{
  const auto default_material = data::MaterialAsset::CreateDefault();
  const auto default_key = default_material->GetAssetKey();

  struct MeshBuffers final {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    std::vector<TriangleRange> ranges;
  };

  auto owner = std::make_shared<MeshBuffers>();
  owner->positions = {
    glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 1.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 1.0F, 0.0F },
  };
  owner->indices = { 0u, 1u, 2u };
  owner->ranges = {
    TriangleRange {
      .material_slot = 0,
      .first_index = 0,
      .index_count = 3,
    },
  };

  TriangleMesh triangle_mesh {
    .mesh_type = data::MeshType::kStandard,
    .streams = MeshStreamView {
      .positions
      = std::span<const glm::vec3>(owner->positions.data(),
        owner->positions.size()),
      .normals = {},
      .texcoords = {},
      .tangents = {},
      .bitangents = {},
      .colors = {},
      .joint_indices = {},
      .joint_weights = {},
    },
    .inverse_bind_matrices = {},
    .joint_remap = {},
    .indices = std::span<const uint32_t>(
      owner->indices.data(), owner->indices.size()),
    .ranges = std::span<const TriangleRange>(
      owner->ranges.data(), owner->ranges.size()),
    .bounds = std::nullopt,
  };

  MeshBuildPipeline::WorkItem item;
  item.source_id = "mesh0";
  item.mesh_name = "Mesh_0";
  item.storage_mesh_name = "Mesh_0";
  item.source_key = nullptr;
  item.lods = {
    MeshLod {
      .lod_name = "LOD0",
      .source = std::move(triangle_mesh),
      .source_owner = std::move(owner),
    },
  };
  item.material_keys = { default_key };
  item.default_material_key = default_key;
  item.request.source_path = "Geometry.fbx";
  item.stop_token = {};
  return item;
}

auto MakeSceneWorkItem(std::shared_ptr<FakeSceneAdapter> adapter)
  -> ScenePipeline::WorkItem
{
  ImportRequest request;
  request.source_path = "Scene.scene";
  static NamingService naming_service(NamingService::Config {
    .strategy = std::make_shared<NoOpNamingStrategy>(),
    .enable_namespacing = false,
    .enforce_uniqueness = false,
  });
  return ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter), "Scene", {},
    {}, std::move(request), oxygen::observer_ptr { &naming_service }, {});
}

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

class PipelineConformanceTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify progress counters update for BufferPipeline.
NOLINT_TEST_F(PipelineConformanceTest, BufferPipeline_ProgressCounters_Update)
{
  // Arrange
  BufferPipeline::WorkResult result;
  PipelineProgress progress;
  ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(BufferPipeline::WorkItem {
        .source_id = "buf0",
        .cooked = MakeBufferPayload(),
        .stop_token = {},
      });
      result = co_await pipeline.Collect();
      progress = pipeline.GetProgress();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
  EXPECT_EQ(progress.submitted, 1u);
  EXPECT_EQ(progress.completed + progress.failed, 1u);
  EXPECT_EQ(progress.in_flight, 0u);
}

//! Verify progress counters update for TexturePipeline.
NOLINT_TEST_F(PipelineConformanceTest, TexturePipeline_ProgressCounters_Update)
{
  // Arrange
  TexturePipeline::WorkResult result;
  ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    TexturePipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeTextureWorkItem());
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
}

//! Verify progress counters update for MaterialPipeline.
NOLINT_TEST_F(PipelineConformanceTest, MaterialPipeline_ProgressCounters_Update)
{
  // Arrange
  MaterialPipeline::WorkResult result;
  ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    MaterialPipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeMaterialWorkItem());
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
}

//! Verify progress counters update for GeometryPipeline.
NOLINT_TEST_F(PipelineConformanceTest, GeometryPipeline_ProgressCounters_Update)
{
  // Arrange
  MeshBuildPipeline::WorkResult result;
  ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    MeshBuildPipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeGeometryWorkItem());
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
}

//! Verify progress counters update for ScenePipeline.
NOLINT_TEST_F(PipelineConformanceTest, ScenePipeline_ProgressCounters_Update)
{
  // Arrange
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;
  ThreadPool pool(loop_, 1);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    ScenePipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeSceneWorkItem(std::move(adapter)));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
}

//! Verify stop tokens cancel BufferPipeline work.
NOLINT_TEST_F(PipelineConformanceTest, BufferPipeline_StopToken_Cancels)
{
  // Arrange
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 1);
  std::stop_source source;
  source.request_stop();

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(BufferPipeline::WorkItem {
        .source_id = "buf0",
        .cooked = MakeBufferPayload(),
        .stop_token = source.get_token(),
      });
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
}

//! Verify stop tokens cancel TexturePipeline work.
NOLINT_TEST_F(PipelineConformanceTest, TexturePipeline_StopToken_Cancels)
{
  // Arrange
  TexturePipeline::WorkResult result;
  ThreadPool pool(loop_, 1);
  std::stop_source source;
  source.request_stop();

  // Act
  co::Run(loop_, [&]() -> Co<> {
    TexturePipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      auto item = MakeTextureWorkItem();
      item.stop_token = source.get_token();
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
}

//! Verify stop tokens cancel ScenePipeline work.
NOLINT_TEST_F(PipelineConformanceTest, ScenePipeline_StopToken_Cancels)
{
  // Arrange
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  ScenePipeline::WorkResult result;
  ThreadPool pool(loop_, 1);
  std::stop_source source;
  source.request_stop();

  // Act
  co::Run(loop_, [&]() -> Co<> {
    ScenePipeline pipeline(pool);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      auto item = MakeSceneWorkItem(std::move(adapter));
      item.stop_token = source.get_token();
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
}

} // namespace
