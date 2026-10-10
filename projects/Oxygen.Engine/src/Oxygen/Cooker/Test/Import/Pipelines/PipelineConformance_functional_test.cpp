//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/BufferPipeline.cpp,
//   Import/Internal/Pipelines/GeometryPipeline.cpp,
//   Import/Internal/Pipelines/MaterialPipeline.cpp,
//   Import/Internal/Pipelines/ScenePipeline.cpp,
//   Import/Internal/Pipelines/TexturePipeline.cpp

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SceneBuildTestSupport.h"
#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Test/Support/PipelineHarness.h>
#include <Oxygen/Cooker/Test/Support/SourceLayoutTestSupport.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MeshType.h>
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
using oxygen::cooker::test::MakeMinimalSceneBuild;
using oxygen::cooker::test::RunPipelineOnce;

namespace {

//=== Helpers
//===--------------------------------------------------------------//

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
    .output_format_policy = TexturePipeline::OutputFormatPolicy::kExplicit,
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
  item.material.name = "Material_0";
  item.material.storage_name = "Material_0";
  item.material.shader_requests = {
    ShaderRequest {
      .shader_type = 1,
      .source_path = "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
      .shader_hash = 0,
    },
    ShaderRequest {
      .shader_type = 2,
      .source_path = "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
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
  owner->indices = { 0U, 1U, 2U };
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
      .source = triangle_mesh,
      .source_owner = std::move(owner),
    },
  };
  item.material_keys = { default_key };
  item.default_material_key = default_key;
  item.request.source_path = "Geometry.fbx";
  static const auto provenance
    = std::make_shared<const MaterialSlotProvenance>(oxygen::Uuid::Generate());
  item.request.material_slot_provenance = provenance;
  item.stop_token = {};
  item.source_layout_witness
    = oxygen::content::import::test::FixtureSourceLayoutWitness(item.lods);
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

class PipelineConformanceTest : public testing::Test {
protected:
  ImportEventLoop loop_;
  ThreadPool pool_ { loop_, 1 };
};

NOLINT_TEST_F(PipelineConformanceTest, BufferPipelineProgressCountersUpdate)
{
  BufferPipeline::WorkResult result;
  PipelineProgress progress;

  co::Run(loop_, [&] -> Co<> {
    BufferPipeline pipeline(pool_);

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

  EXPECT_TRUE(result.success);
  EXPECT_EQ(progress.submitted, 1U);
  EXPECT_EQ(progress.completed + progress.failed, 1U);
  EXPECT_EQ(progress.in_flight, 0U);
}

NOLINT_TEST_F(PipelineConformanceTest, TexturePipelineProgressCountersUpdate)
{
  const auto result
    = RunPipelineOnce<TexturePipeline>(loop_, MakeTextureWorkItem(), pool_);

  EXPECT_TRUE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, MaterialPipelineProgressCountersUpdate)
{
  const auto result
    = RunPipelineOnce<MaterialPipeline>(loop_, MakeMaterialWorkItem(), pool_);

  EXPECT_TRUE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, GeometryPipelineProgressCountersUpdate)
{
  const auto result
    = RunPipelineOnce<MeshBuildPipeline>(loop_, MakeGeometryWorkItem(), pool_);

  EXPECT_TRUE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, ScenePipelineProgressCountersUpdate)
{
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  const auto result = RunPipelineOnce<ScenePipeline>(
    loop_, MakeSceneWorkItem(std::move(adapter)), pool_);

  EXPECT_TRUE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, BufferPipelineStopTokenCancels)
{
  std::stop_source source;
  source.request_stop();

  const auto result = RunPipelineOnce<BufferPipeline>(loop_,
    BufferPipeline::WorkItem {
      .source_id = "buf0",
      .cooked = MakeBufferPayload(),
      .stop_token = source.get_token(),
    },
    pool_);

  EXPECT_FALSE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, TexturePipelineStopTokenCancels)
{
  std::stop_source source;
  source.request_stop();
  auto item = MakeTextureWorkItem();
  item.stop_token = source.get_token();

  const auto result
    = RunPipelineOnce<TexturePipeline>(loop_, std::move(item), pool_);

  EXPECT_FALSE(result.success);
}

NOLINT_TEST_F(PipelineConformanceTest, ScenePipelineStopTokenCancels)
{
  auto adapter = std::make_shared<FakeSceneAdapter>();
  adapter->build = MakeMinimalSceneBuild("Root");

  std::stop_source source;
  source.request_stop();
  auto item = MakeSceneWorkItem(std::move(adapter));
  item.stop_token = source.get_token();

  const auto result
    = RunPipelineOnce<ScenePipeline>(loop_, std::move(item), pool_);

  EXPECT_FALSE(result.success);
}

} // namespace
