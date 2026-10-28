//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/GeometryPipeline.cpp,
//   Import/Internal/Pipelines/MeshBuildPipeline.cpp

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/PipelineHarness.h>
#include <Oxygen/Cooker/test/Support/SourceLayoutTestSupport.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using co::kJoin;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::RunPipelineOnce;

  //=== Test Helpers
  //===---------------------------------------------------------//

  constexpr uint32_t kGeomAttr_Normal = 1U << 0U;
  constexpr uint32_t kGeomAttr_Tangent = 1U << 1U;
  constexpr uint32_t kGeomAttr_Bitangent = 1U << 2U;
  constexpr uint32_t kGeomAttr_Texcoord0 = 1U << 3U;
  constexpr uint32_t kGeomAttr_Color0 = 1U << 4U;
  constexpr uint32_t kGeomAttr_JointWeights = 1U << 5U;
  constexpr uint32_t kGeomAttr_JointIndices = 1U << 6U;

  struct MeshBuffers {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> tangents;
    std::vector<glm::vec3> bitangents;
    std::vector<glm::vec4> colors;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<uint32_t> joint_remap;
    std::vector<uint32_t> indices;
    std::vector<TriangleRange> ranges;
  };

  [[nodiscard]] auto MakeDefaultMaterialKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/test/Materials/Default.omat");
  }

  [[nodiscard]] auto MakeTriangleMeshBuffers() -> std::shared_ptr<MeshBuffers>
  {
    auto buffers = std::make_shared<MeshBuffers>();
    buffers->positions = {
      glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 1.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 1.0F, 0.0F },
    };
    buffers->normals = {
      glm::vec3 { 0.0F, 0.0F, 1.0F },
      glm::vec3 { 0.0F, 0.0F, 1.0F },
      glm::vec3 { 0.0F, 0.0F, 1.0F },
    };
    buffers->texcoords = {
      glm::vec2 { 0.0F, 0.0F },
      glm::vec2 { 1.0F, 0.0F },
      glm::vec2 { 0.0F, 1.0F },
    };
    buffers->indices = { 0, 1, 2 };
    buffers->ranges = {
      TriangleRange {
        .surface = 0,
        .first_index = 0,
        .index_count = 3,
      },
    };
    return buffers;
  }

  [[nodiscard]] auto MakeSkinnedTriangleMeshBuffers()
    -> std::shared_ptr<MeshBuffers>
  {
    auto buffers = MakeTriangleMeshBuffers();
    buffers->joint_indices = {
      glm::uvec4 { 0, 1, 2, 0 },
      glm::uvec4 { 0, 1, 2, 0 },
      glm::uvec4 { 0, 1, 2, 0 },
    };
    constexpr auto kWeights = glm::vec4 { 0.5F, 0.3F, 0.2F, 0.0F };
    buffers->joint_weights = { kWeights, kWeights, kWeights };
    buffers->inverse_bind_matrices = {
      glm::mat4(1.0F),
      glm::mat4(1.0F),
      glm::mat4(1.0F),
    };
    buffers->joint_remap = { 0, 1, 2 };
    return buffers;
  }

  [[nodiscard]] auto MakeTriangleMesh(const MeshBuffers& buffers)
    -> TriangleMesh
  {
    return TriangleMesh {
    .mesh_type = data::MeshType::kStandard,
    .streams = MeshStreamView {
      .positions = std::span<const glm::vec3>(
        buffers.positions.data(), buffers.positions.size()),
      .normals = std::span<const glm::vec3>(
        buffers.normals.data(), buffers.normals.size()),
      .texcoords = std::span<const glm::vec2>(
        buffers.texcoords.data(), buffers.texcoords.size()),
      .tangents = std::span<const glm::vec3>(
        buffers.tangents.data(), buffers.tangents.size()),
      .bitangents = std::span<const glm::vec3>(
        buffers.bitangents.data(), buffers.bitangents.size()),
      .colors = std::span<const glm::vec4>(
        buffers.colors.data(), buffers.colors.size()),
      .joint_indices = std::span<const glm::uvec4>(
        buffers.joint_indices.data(), buffers.joint_indices.size()),
      .joint_weights = std::span<const glm::vec4>(
        buffers.joint_weights.data(), buffers.joint_weights.size()),
    },
    .inverse_bind_matrices = std::span<const glm::mat4>(
      buffers.inverse_bind_matrices.data(),
      buffers.inverse_bind_matrices.size()),
    .joint_remap = std::span<const uint32_t>(
      buffers.joint_remap.data(), buffers.joint_remap.size()),
    .indices = std::span<const uint32_t>(
      buffers.indices.data(), buffers.indices.size()),
    .ranges = std::span<const TriangleRange>(
      buffers.ranges.data(), buffers.ranges.size()),
    .bounds = std::nullopt,
  };
  }

  [[nodiscard]] auto MakeSkinnedTriangleMesh(const MeshBuffers& buffers)
    -> TriangleMesh
  {
    auto mesh = MakeTriangleMesh(buffers);
    mesh.mesh_type = data::MeshType::kSkinned;
    return mesh;
  }

  [[nodiscard]] auto MakeProceduralTriangleMesh(const MeshBuffers& buffers)
    -> TriangleMesh
  {
    auto mesh = MakeTriangleMesh(buffers);
    mesh.mesh_type = data::MeshType::kProcedural;
    return mesh;
  }

  [[nodiscard]] auto MakeRequest() -> ImportRequest
  {
    ImportRequest request;
    static const auto provenance
      = std::make_shared<const SurfaceProvenance>(Uuid::Generate());
    request.surface_provenance = provenance;
    request.source_path = "Geometry.fbx";
    return request;
  }

  [[nodiscard]] auto MakeWorkItem(std::shared_ptr<MeshBuffers> buffers)
    -> MeshBuildPipeline::WorkItem
  {
    MeshBuildPipeline::WorkItem item;
    item.source_id = "mesh0";
    item.mesh_name = "Mesh_0";
    item.storage_mesh_name = "Mesh_0";
    item.material_keys = { MakeDefaultMaterialKey() };
    item.default_material_key = item.material_keys.front();
    item.want_textures = true;
    item.has_material_textures = true;
    item.request = MakeRequest();

    TriangleMesh mesh = MakeTriangleMesh(*buffers);
    item.lods = {
      MeshLod {
        .lod_name = "LOD0",
        .source = mesh,
        .source_owner = std::move(buffers),
      },
    };
    item.source_layout_witness = test::FixtureSourceLayoutWitness(item.lods);
    return item;
  }

  [[nodiscard]] auto MakeSkinnedWorkItem(std::shared_ptr<MeshBuffers> buffers)
    -> MeshBuildPipeline::WorkItem
  {
    MeshBuildPipeline::WorkItem item;
    item.source_id = "mesh0";
    item.mesh_name = "Mesh_0";
    item.storage_mesh_name = "Mesh_0";
    item.material_keys = { MakeDefaultMaterialKey() };
    item.default_material_key = item.material_keys.front();
    item.want_textures = true;
    item.has_material_textures = true;
    item.request = MakeRequest();

    TriangleMesh mesh = MakeSkinnedTriangleMesh(*buffers);
    item.lods = {
      MeshLod {
        .lod_name = "LOD0",
        .source = mesh,
        .source_owner = std::move(buffers),
      },
    };
    item.source_layout_witness = test::FixtureSourceLayoutWitness(item.lods);
    return item;
  }

  [[nodiscard]] auto MakeProceduralWorkItem(
    std::shared_ptr<MeshBuffers> buffers) -> MeshBuildPipeline::WorkItem
  {
    MeshBuildPipeline::WorkItem item;
    item.source_id = "mesh0";
    item.mesh_name = "Mesh_0";
    item.storage_mesh_name = "Mesh_0";
    item.material_keys = { MakeDefaultMaterialKey() };
    item.default_material_key = item.material_keys.front();
    item.want_textures = false;
    item.has_material_textures = false;
    item.request = MakeRequest();

    TriangleMesh mesh = MakeProceduralTriangleMesh(*buffers);
    item.lods = {
      MeshLod {
        .lod_name = "LOD0",
        .source = mesh,
        .source_owner = std::move(buffers),
      },
    };
    item.source_layout_witness = test::FixtureSourceLayoutWitness(item.lods);
    return item;
  }

  [[nodiscard]] auto MakeWorkItemWithLods(
    const std::shared_ptr<MeshBuffers>& buffers, const uint32_t lod_count)
    -> MeshBuildPipeline::WorkItem
  {
    auto item = MakeWorkItem(buffers);
    item.lods.clear();
    item.lods.reserve(lod_count);

    for (uint32_t lod_i = 0; lod_i < lod_count; ++lod_i) {
      TriangleMesh mesh = MakeTriangleMesh(*buffers);
      item.lods.push_back(MeshLod {
        .lod_name = "LOD" + std::to_string(lod_i),
        .source = mesh,
        .source_owner = buffers,
      });
    }

    item.source_layout_witness = test::FixtureSourceLayoutWitness(item.lods);
    return item;
  }

  template <typename T>
  [[nodiscard]] auto ReadStructAt(
    const std::vector<std::byte>& bytes, const size_t offset) -> T
  {
    T out {};
    if (bytes.size() < offset + sizeof(T)) {
      return out;
    }
    std::memcpy(&out, std::span(bytes).subspan(offset).data(), sizeof(T));
    return out;
  }

  //=== Basic Behavior Tests
  //===-----------------------------------------------------//

  class GeometryPipelineTest : public testing::Test {
  protected:
    ImportEventLoop loop_;
    co::ThreadPool pool_ { loop_, 2 };
  };

  NOLINT_TEST_F(
    GeometryPipelineTest, CollectWithSingleTriangleEmitsCookedPayload)
  {
    const auto buffers = MakeTriangleMeshBuffers();

    const auto result
      = RunPipelineOnce<MeshBuildPipeline>(loop_, MakeWorkItem(buffers), pool_,
        MeshBuildPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .with_content_hashing = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";
    EXPECT_TRUE(result.diagnostics.empty());

    const auto& cooked = *result.cooked;
    ASSERT_EQ(cooked.lods.size(), 1U);

    const auto& lod0 = cooked.lods.front();
    EXPECT_TRUE(lod0.auxiliary_buffers.empty());
    EXPECT_EQ(lod0.vertex_buffer.data.size(), sizeof(data::Vertex) * 3U);
    EXPECT_EQ(lod0.index_buffer.data.size(), sizeof(uint32_t) * 3U);

    const auto& bytes = cooked.descriptor_bytes;
    ASSERT_GE(bytes.size(), sizeof(data::pak::geometry::GeometryAssetDesc));

    const auto asset_desc
      = ReadStructAt<data::pak::geometry::GeometryAssetDesc>(bytes, 0);
    EXPECT_EQ(asset_desc.header.asset_type,
      static_cast<uint8_t>(data::AssetType::kGeometry));
    EXPECT_EQ(
      asset_desc.header.version, data::pak::geometry::kGeometryAssetVersion);
    EXPECT_EQ(asset_desc.lod_count, 1U);
    EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Normal), 0U);
    EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Tangent), 0U);
    EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Bitangent), 0U);
    EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Texcoord0), 0U);
    EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_Color0), 0U);
    EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_JointIndices), 0U);
    EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_JointWeights), 0U);

    size_t offset = sizeof(data::pak::geometry::GeometryAssetDesc);
    const auto mesh_desc
      = ReadStructAt<data::pak::geometry::MeshDesc>(bytes, offset);
    EXPECT_EQ(mesh_desc.submesh_count, 1U);
    EXPECT_EQ(mesh_desc.mesh_view_count, 1U);
    EXPECT_EQ(
      mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kStandard));

    offset += sizeof(data::pak::geometry::MeshDesc);
    const auto submesh_desc
      = ReadStructAt<data::pak::geometry::SubMeshDesc>(bytes, offset);
    EXPECT_EQ(submesh_desc.mesh_view_count, 1U);
    EXPECT_EQ(submesh_desc.material_asset_key, MakeDefaultMaterialKey());

    offset += sizeof(data::pak::geometry::SubMeshDesc);
    const auto view_desc
      = ReadStructAt<data::pak::geometry::MeshViewDesc>(bytes, offset);
    EXPECT_EQ(view_desc.first_index, 0U);
    EXPECT_EQ(view_desc.index_count, 3U);
    EXPECT_EQ(view_desc.vertex_count, 3U);
  }

  //! Names longer than 63 bytes and non-ASCII names are stored whole.
  NOLINT_TEST_F(GeometryPipelineTest, CollectStoresLongNamesWhole)
  {
    const auto buffers = MakeTriangleMeshBuffers();

    auto item = MakeWorkItem(buffers);
    item.mesh_name = std::string(200, 'M') + "\xC3\xA9";
    item.storage_mesh_name = item.mesh_name;
    const auto item_mesh_name = item.mesh_name;

    const auto result
      = RunPipelineOnce<MeshBuildPipeline>(loop_, std::move(item), pool_,
        MeshBuildPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .with_content_hashing = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked);
    const auto& bytes = result.cooked.value().descriptor_bytes;
    const auto asset
      = ReadStructAt<data::pak::geometry::GeometryAssetDesc>(bytes, 0);
    const auto mesh = ReadStructAt<data::pak::geometry::MeshDesc>(
      bytes, sizeof(data::pak::geometry::GeometryAssetDesc));
    EXPECT_EQ(oxygen::content::test::ReadDescriptorString(
                bytes, asset.header.name_offset),
      item_mesh_name);
    EXPECT_EQ(
      oxygen::content::test::ReadDescriptorString(bytes, mesh.name_offset),
      item_mesh_name);
  }

  NOLINT_TEST_F(
    GeometryPipelineTest, CollectWithSkinnedMeshSerializesInfoInMeshDesc)
  {
    const auto buffers = MakeSkinnedTriangleMeshBuffers();

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeSkinnedWorkItem(buffers), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";
    EXPECT_TRUE(result.diagnostics.empty());

    const auto& cooked = *result.cooked;
    ASSERT_EQ(cooked.lods.size(), 1U);

    const auto& bytes = cooked.descriptor_bytes;
    ASSERT_GE(bytes.size(), sizeof(data::pak::geometry::GeometryAssetDesc));

    size_t offset = sizeof(data::pak::geometry::GeometryAssetDesc);
    const auto mesh_desc
      = ReadStructAt<data::pak::geometry::MeshDesc>(bytes, offset);
    EXPECT_EQ(
      mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kSkinned));
    EXPECT_EQ(mesh_desc.submesh_count, 1U);
    EXPECT_EQ(mesh_desc.mesh_view_count, 1U);
    EXPECT_EQ(mesh_desc.info.skinned.joint_count, 3U);
    EXPECT_EQ(mesh_desc.info.skinned.influences_per_vertex, 4U);

    offset += sizeof(data::pak::geometry::MeshDesc);
    const auto submesh_desc
      = ReadStructAt<data::pak::geometry::SubMeshDesc>(bytes, offset);
    EXPECT_EQ(submesh_desc.mesh_view_count, 1U);
    EXPECT_EQ(submesh_desc.material_asset_key, MakeDefaultMaterialKey());

    offset += sizeof(data::pak::geometry::SubMeshDesc);
    const auto view_desc
      = ReadStructAt<data::pak::geometry::MeshViewDesc>(bytes, offset);
    EXPECT_EQ(view_desc.first_index, 0U);
    EXPECT_EQ(view_desc.index_count, 3U);
    EXPECT_EQ(view_desc.vertex_count, 3U);
  }

  //! Skin data that cannot bind the mesh is dropped with a warning; the mesh
  //! cooks as static geometry.
  NOLINT_TEST_F(
    GeometryPipelineTest, CollectSkinnedMissingInverseBindCooksStatic)
  {
    auto buffers = MakeSkinnedTriangleMeshBuffers();
    buffers->inverse_bind_matrices.clear();

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeSkinnedWorkItem(buffers), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.cooked.has_value());
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, "mesh.missing_inverse_bind"));
  }

  NOLINT_TEST_F(GeometryPipelineTest, CollectWithProceduralMeshReturnsFailure)
  {
    const auto buffers = MakeTriangleMeshBuffers();

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeProceduralWorkItem(buffers), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    ASSERT_FALSE(result.success);
    EXPECT_FALSE(result.cooked.has_value());
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, "mesh.procedural_unsupported"));
  }

  NOLINT_TEST_F(GeometryPipelineTest, FinalizeDescriptorPatchesIndicesAndHash)
  {
    const auto buffers = MakeTriangleMeshBuffers();
    MeshBuildPipeline::WorkResult result;
    std::vector<ImportDiagnostic> diagnostics;
    std::optional<GeometryPipeline::FinalizedDescriptor> finalized;

    // co::Run blocks until the coroutine completes.
    co::Run(loop_, [&] -> co::Co<> {
      MeshBuildPipeline pipeline(pool_,
        MeshBuildPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .with_content_hashing = true,
        });
      GeometryPipeline finalizer(
        pool_, GeometryPipeline::Config { .with_content_hashing = true });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);
        co_await pipeline.Submit(MakeWorkItem(buffers));
        result = co_await pipeline.Collect();
        pipeline.Close();

        if (!result.success || !result.cooked.has_value()) {
          co_return kJoin;
        }

        const MeshBufferBindings bindings {
          .vertex_buffer = data::pak::core::ResourceIndexT { 11U },
          .index_buffer = data::pak::core::ResourceIndexT { 22U },
        };

        finalized = co_await finalizer.FinalizeDescriptor(
          std::span<const MeshBufferBindings>(&bindings, 1),
          result.cooked->descriptor_bytes,
          std::span<const GeometryPipeline::MaterialKeyPatch> {}, diagnostics);
        co_return kJoin;
      };
    });

    ASSERT_HAS_VALUE(finalized) << "Expected finalized to contain a value";
    ASSERT_TRUE(diagnostics.empty());

    const auto& bytes = finalized->bytes;
    const auto asset_desc
      = ReadStructAt<data::pak::geometry::GeometryAssetDesc>(bytes, 0);
    EXPECT_FALSE(base::IsAllZero(asset_desc.header.content_hash));

    size_t offset = sizeof(data::pak::geometry::GeometryAssetDesc);
    const auto mesh_desc
      = ReadStructAt<data::pak::geometry::MeshDesc>(bytes, offset);
    const auto vertex = finalized->references.ResolveResource(
      mesh_desc.info.standard.vertex_buffer, data::ResourceKind::kBuffer);
    const auto index = finalized->references.ResolveResource(
      mesh_desc.info.standard.index_buffer, data::ResourceKind::kBuffer);
    ASSERT_HAS_VALUE(vertex);
    ASSERT_HAS_VALUE(index);
    EXPECT_EQ(
      *vertex, std::optional { data::pak::core::ResourceIndexT { 11U } });
    EXPECT_EQ(
      *index, std::optional { data::pak::core::ResourceIndexT { 22U } });
  }

  NOLINT_TEST_F(GeometryPipelineTest, CollectWithMissingPositionsReturnsFailure)
  {
    auto buffers = std::make_shared<MeshBuffers>();
    buffers->indices = { 0, 1, 2 };
    buffers->ranges = {
      TriangleRange {
        .surface = 0,
        .first_index = 0,
        .index_count = 3,
      },
    };

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeWorkItem(std::move(buffers)), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cooked.has_value());
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, "mesh.missing_positions"));
  }

  NOLINT_TEST_F(
    GeometryPipelineTest, CollectWithVertexBufferTooLargeReturnsFailure)
  {
    const auto buffers = MakeTriangleMeshBuffers();

    const auto result
      = RunPipelineOnce<MeshBuildPipeline>(loop_, MakeWorkItem(buffers), pool_,
        MeshBuildPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .with_content_hashing = true,
          .max_data_blob_bytes = sizeof(data::Vertex) * 2U,
        });

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cooked.has_value());
    EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.buffer_too_large"));
  }

  NOLINT_TEST_F(
    GeometryPipelineTest, CollectWithSkinnedBufferTooLargeReturnsFailure)
  {
    const auto buffers = MakeSkinnedTriangleMeshBuffers();

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeSkinnedWorkItem(buffers), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
        .max_data_blob_bytes = sizeof(glm::uvec4) * 2U,
      });

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cooked.has_value());
    EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.buffer_too_large"));
  }

  NOLINT_TEST_F(GeometryPipelineTest, CollectWithTooManyLodsReturnsFailure)
  {
    const auto buffers = MakeTriangleMeshBuffers();
    constexpr uint32_t kTooManyLods = 9U;

    const auto result = RunPipelineOnce<MeshBuildPipeline>(loop_,
      MakeWorkItemWithLods(buffers, kTooManyLods), pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cooked.has_value());
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, "mesh.invalid_lod_count"));
  }

  //=== Mesh View Split Tests
  //===--------------------------------------------------//

  //! A flat grid of `columns` x `rows` quads, two triangles each.
  [[nodiscard]] auto MakeGridMeshBuffers(
    const uint32_t columns, const uint32_t rows) -> std::shared_ptr<MeshBuffers>
  {
    auto buffers = std::make_shared<MeshBuffers>();
    for (uint32_t y = 0U; y <= rows; ++y) {
      for (uint32_t x = 0U; x <= columns; ++x) {
        buffers->positions.emplace_back(
          static_cast<float>(x), static_cast<float>(y), 0.0F);
        buffers->normals.emplace_back(0.0F, 0.0F, 1.0F);
        buffers->texcoords.emplace_back(
          static_cast<float>(x) / static_cast<float>(columns),
          static_cast<float>(y) / static_cast<float>(rows));
      }
    }
    const auto corner
      = [columns](const uint32_t x, const uint32_t y) -> uint32_t {
      return (y * (columns + 1U)) + x;
    };
    for (uint32_t y = 0U; y < rows; ++y) {
      for (uint32_t x = 0U; x < columns; ++x) {
        for (const auto index : {
               corner(x, y),
               corner(x + 1U, y),
               corner(x + 1U, y + 1U),
               corner(x, y),
               corner(x + 1U, y + 1U),
               corner(x, y + 1U),
             }) {
          buffers->indices.push_back(index);
        }
      }
    }
    buffers->ranges = {
      TriangleRange {
        .surface = 0,
        .first_index = 0,
        .index_count = static_cast<uint32_t>(buffers->indices.size()),
      },
    };
    return buffers;
  }

  //! The grid with every vertex bound to joint 0, for skinned imports.
  [[nodiscard]] auto MakeSkinnedGridMeshBuffers(
    const uint32_t columns, const uint32_t rows) -> std::shared_ptr<MeshBuffers>
  {
    auto buffers = MakeGridMeshBuffers(columns, rows);
    buffers->joint_indices.assign(buffers->positions.size(), glm::uvec4 { 0U });
    buffers->joint_weights.assign(
      buffers->positions.size(), glm::vec4 { 1.0F, 0.0F, 0.0F, 0.0F });
    buffers->inverse_bind_matrices = { glm::mat4(1.0F) };
    buffers->joint_remap = { 0U };
    return buffers;
  }

  //! The views of the only submesh of the only LOD of a cooked geometry.
  struct CookedViews {
    data::pak::geometry::SubMeshDesc submesh {};
    std::vector<data::pak::geometry::MeshViewDesc> views;
    std::vector<data::Vertex> vertices;
    std::vector<uint32_t> indices;
  };

  [[nodiscard]] auto ReadCookedViews(
    const MeshBuildPipeline::CookedGeometryPayload& cooked) -> CookedViews
  {
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    auto result = CookedViews {};
    auto offset = sizeof(GeometryAssetDesc) + sizeof(MeshDesc);
    result.submesh = ReadStructAt<SubMeshDesc>(cooked.descriptor_bytes, offset);
    offset += sizeof(SubMeshDesc);
    for (uint32_t view = 0U; view < result.submesh.mesh_view_count; ++view) {
      result.views.push_back(
        ReadStructAt<MeshViewDesc>(cooked.descriptor_bytes, offset));
      offset += sizeof(MeshViewDesc);
    }
    const auto& lod = cooked.lods.front();
    result.vertices.resize(
      lod.vertex_buffer.data.size() / sizeof(data::Vertex));
    std::memcpy(result.vertices.data(), lod.vertex_buffer.data.data(),
      result.vertices.size() * sizeof(data::Vertex));
    result.indices.resize(lod.index_buffer.data.size() / sizeof(uint32_t));
    std::memcpy(result.indices.data(), lod.index_buffer.data.data(),
      result.indices.size() * sizeof(uint32_t));
    return result;
  }

  class GeometryPipelineSplitTest : public GeometryPipelineTest {
  protected:
    auto Cook(MeshBuildPipeline::WorkItem item) -> MeshBuildPipeline::WorkResult
    {
      return RunPipelineOnce<MeshBuildPipeline>(loop_, std::move(item), pool_,
        MeshBuildPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .with_content_hashing = false,
        });
    }
  };

  //! A static submesh above the view limit becomes deterministic views of at
  //! most `kMaxMeshViewTriangles`, each one contiguous index and vertex range,
  //! its triangles sharing their vertices, with tight bounds inside the
  //! submesh bounds.
  NOLINT_TEST_F(GeometryPipelineSplitTest, LargeStaticSubmeshSplitsIntoViews)
  {
    // 80 x 64 quads = 10240 triangles.
    constexpr auto kMax = MeshBuildPipeline::kMaxMeshViewTriangles;
    const auto buffers = MakeGridMeshBuffers(80U, 64U);
    const auto triangle_count
      = static_cast<uint32_t>(buffers->indices.size() / 3U);

    const auto first = Cook(MakeWorkItem(buffers));
    const auto second = Cook(MakeWorkItem(buffers));

    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);
    ASSERT_HAS_VALUE(first.cooked)
      << "Expected both imports to produce cooked geometry";
    ASSERT_HAS_VALUE(second.cooked)
      << "Expected both imports to produce cooked geometry";
    const auto& cooked = first.cooked.value();
    EXPECT_EQ(cooked.descriptor_bytes, second.cooked.value().descriptor_bytes);
    EXPECT_EQ(cooked.lods.front().vertex_buffer.data,
      second.cooked.value().lods.front().vertex_buffer.data);
    EXPECT_EQ(cooked.lods.front().index_buffer.data,
      second.cooked.value().lods.front().index_buffer.data);

    const auto result = ReadCookedViews(cooked);
    ASSERT_EQ(result.views.size(), (triangle_count + kMax - 1U) / kMax);
    uint32_t next_index = 0U;
    uint32_t next_vertex = 0U;
    for (const auto& view : result.views) {
      EXPECT_LE(view.index_count, kMax * 3U);
      EXPECT_EQ(view.first_index, next_index);
      EXPECT_EQ(view.first_vertex, next_vertex);
      EXPECT_LT(view.vertex_count, view.index_count);
      next_index += view.index_count;
      next_vertex += view.vertex_count;

      auto min = glm::vec3(std::numeric_limits<float>::max());
      auto max = glm::vec3(std::numeric_limits<float>::lowest());
      for (uint32_t i = 0U; i < view.index_count; ++i) {
        const auto relative = result.indices.at(view.first_index + i);
        ASSERT_LT(relative, view.vertex_count);
        const auto& position
          = result.vertices.at(view.first_vertex + relative).position;
        min = glm::min(min, position);
        max = glm::max(max, position);
      }
      EXPECT_EQ(std::to_array(view.bounding_box_min),
        (std::array { min.x, min.y, min.z }));
      EXPECT_EQ(std::to_array(view.bounding_box_max),
        (std::array { max.x, max.y, max.z }));
      const auto submesh_min = std::to_array(result.submesh.bounding_box_min);
      const auto submesh_max = std::to_array(result.submesh.bounding_box_max);
      for (size_t axis = 0; axis < submesh_min.size(); ++axis) {
        EXPECT_GE(
          std::to_array(view.bounding_box_min).at(axis), submesh_min.at(axis));
        EXPECT_LE(
          std::to_array(view.bounding_box_max).at(axis), submesh_max.at(axis));
      }
    }
    EXPECT_EQ(next_index, triangle_count * 3U);
    EXPECT_EQ(next_vertex, static_cast<uint32_t>(result.vertices.size()));
  }

  NOLINT_TEST_F(GeometryPipelineSplitTest, SmallStaticSubmeshKeepsOneView)
  {
    const auto result = Cook(MakeWorkItem(MakeGridMeshBuffers(4U, 2U)));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto views = ReadCookedViews(result.cooked.value());
    ASSERT_EQ(views.views.size(), 1U);
    const auto& view = views.views.front();
    EXPECT_EQ(view.bounding_box_min[0], 0.0F);
    EXPECT_EQ(view.bounding_box_min[1], 0.0F);
    EXPECT_EQ(view.bounding_box_max[0], 4.0F);
    EXPECT_EQ(view.bounding_box_max[1], 2.0F);
  }

  //! Skinned submeshes are never split: deformation can move triangles out of
  //! a run's bind-pose bounds.
  NOLINT_TEST_F(GeometryPipelineSplitTest, LargeSkinnedSubmeshIsNotSplit)
  {
    const auto buffers = MakeSkinnedGridMeshBuffers(80U, 64U);

    const auto result = Cook(MakeSkinnedWorkItem(buffers));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto views = ReadCookedViews(result.cooked.value());
    ASSERT_EQ(views.views.size(), 1U);
    EXPECT_EQ(views.views.front().index_count, buffers->indices.size());
  }

  //=== Welding And Generated LODs
  //===-----------------------------------------------//

  //! Corners that agree on every attribute share one vertex: a 4 x 2 quad grid
  //! cooks its 15 grid points, not one vertex per corner.
  NOLINT_TEST_F(GeometryPipelineSplitTest, EqualCornersShareOneVertex)
  {
    const auto result = Cook(MakeWorkItem(MakeGridMeshBuffers(4U, 2U)));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto views = ReadCookedViews(result.cooked.value());
    EXPECT_EQ(views.vertices.size(), 15U);
    EXPECT_EQ(views.indices.size(), 48U);
  }

  //! What a cooked geometry's descriptor says of each LOD.
  struct CookedLod {
    std::vector<data::SurfaceId> surfaces;
  };

  //! The asset descriptor and each LOD's submesh surfaces of a cooked
  //! standard geometry.
  struct CookedLods {
    data::pak::geometry::GeometryAssetDesc asset {};
    std::vector<CookedLod> lods;
  };

  [[nodiscard]] auto ReadCookedLods(
    const MeshBuildPipeline::CookedGeometryPayload& cooked) -> CookedLods
  {
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    auto result = CookedLods {};
    result.asset = ReadStructAt<GeometryAssetDesc>(cooked.descriptor_bytes, 0);
    auto offset = sizeof(GeometryAssetDesc);
    for (uint32_t lod = 0U; lod < result.asset.lod_count; ++lod) {
      const auto mesh = ReadStructAt<MeshDesc>(cooked.descriptor_bytes, offset);
      offset += sizeof(MeshDesc);
      auto& read = result.lods.emplace_back();
      for (uint32_t submesh = 0U; submesh < mesh.submesh_count; ++submesh) {
        const auto desc
          = ReadStructAt<SubMeshDesc>(cooked.descriptor_bytes, offset);
        offset += sizeof(SubMeshDesc)
          + (static_cast<size_t>(desc.mesh_view_count) * sizeof(MeshViewDesc));
        read.surfaces.push_back(desc.surface_id);
      }
    }
    return result;
  }

  [[nodiscard]] auto WithGeneratedLods(MeshBuildPipeline::WorkItem item,
    const uint32_t levels) -> MeshBuildPipeline::WorkItem
  {
    item.request.options.lod_generation = LodGenerationOptions {
      .enabled = true,
      .levels = levels,
      .reduction_percent = 50.0F,
    };
    return item;
  }

  //! Generate LODs adds the requested levels after LOD 0, each with at most
  //! three quarters of the previous level's triangles and LOD 0's surfaces,
  //! switched by a screen-space error policy with a boundary per level.
  NOLINT_TEST_F(GeometryPipelineSplitTest, GeneratedLodsReduceEachLevel)
  {
    const auto result = Cook(
      WithGeneratedLods(MakeWorkItem(MakeGridMeshBuffers(32U, 32U)), 3U));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    EXPECT_FALSE(
      HasDiagnosticCode(result.diagnostics, "import.lod_generation.stopped"));
    const auto& cooked = result.cooked.value();
    ASSERT_EQ(cooked.lods.size(), 4U);
    for (size_t lod = 1U; lod < cooked.lods.size(); ++lod) {
      EXPECT_LE(cooked.lods.at(lod).index_buffer.data.size() * 4U,
        cooked.lods.at(lod - 1U).index_buffer.data.size() * 3U);
    }
    const auto read = ReadCookedLods(cooked);
    ASSERT_EQ(read.lods.size(), 4U);
    for (const auto& lod : read.lods) {
      EXPECT_EQ(lod.surfaces, read.lods.front().surfaces);
    }
    const auto& policy = read.asset.default_lod_policy;
    EXPECT_EQ(
      policy.type, data::pak::geometry::LodPolicyType::kScreenSpaceError);
    EXPECT_EQ(policy.boundary_count, 3U);
  }

  //! A skinned geometry's generated LODs carry one joint and weight entry per
  //! vertex of their own.
  NOLINT_TEST_F(GeometryPipelineSplitTest, GeneratedSkinnedLodsKeepTheirJoints)
  {
    const auto result = Cook(WithGeneratedLods(
      MakeSkinnedWorkItem(MakeSkinnedGridMeshBuffers(32U, 32U)), 2U));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto& cooked = result.cooked.value();
    ASSERT_EQ(cooked.lods.size(), 3U);
    for (const auto& lod : cooked.lods) {
      const auto vertex_count
        = lod.vertex_buffer.data.size() / sizeof(data::Vertex);
      ASSERT_GE(lod.auxiliary_buffers.size(), 2U);
      EXPECT_EQ(lod.auxiliary_buffers.at(0).data.size(),
        vertex_count * sizeof(glm::uvec4));
      EXPECT_EQ(lod.auxiliary_buffers.at(1).data.size(),
        vertex_count * sizeof(glm::vec4));
    }
  }

  //! A geometry with authored LODs keeps them and says so.
  NOLINT_TEST_F(GeometryPipelineSplitTest, AuthoredLodsSkipGeneration)
  {
    const auto result = Cook(WithGeneratedLods(
      MakeWorkItemWithLods(MakeGridMeshBuffers(8U, 8U), 2U), 3U));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    EXPECT_EQ(result.cooked.value().lods.size(), 2U);
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, "import.lod_generation.skipped"));
  }

  //! A geometry that cannot be reduced cooks its LOD 0 alone and reports
  //! where the chain stopped.
  NOLINT_TEST_F(GeometryPipelineSplitTest, UnreducibleGeometryStopsTheChain)
  {
    const auto result
      = Cook(WithGeneratedLods(MakeWorkItem(MakeTriangleMeshBuffers()), 3U));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    EXPECT_EQ(result.cooked.value().lods.size(), 1U);
    const auto stopped = std::ranges::find(result.diagnostics,
      "import.lod_generation.stopped", &ImportDiagnostic::code);
    ASSERT_NE(stopped, result.diagnostics.end());
    EXPECT_THAT(stopped->message,
      testing::HasSubstr("LOD 1 kept 1 triangles, more than the 0"));
  }

  //! Quad counts around and along a sphere.
  struct SphereTessellation {
    uint32_t segments;
    uint32_t rings;
  };

  //! A closed sphere of `segments` x `rings` quads with positions only, its
  //! vertices shared by the triangles around them, as an indexed source
  //! without normals or UVs.
  [[nodiscard]] auto MakeSphereMeshBuffers(
    const SphereTessellation tessellation) -> std::shared_ptr<MeshBuffers>
  {
    const auto [segments, rings] = tessellation;
    auto buffers = std::make_shared<MeshBuffers>();
    buffers->positions.emplace_back(0.0F, 0.0F, 1.0F);
    for (uint32_t ring = 1U; ring < rings; ++ring) {
      const auto polar = std::numbers::pi_v<float>
        * static_cast<float>(ring) / static_cast<float>(rings);
      for (uint32_t segment = 0U; segment < segments; ++segment) {
        const auto azimuth = 2.0F * std::numbers::pi_v<float>
          * static_cast<float>(segment) / static_cast<float>(segments);
        buffers->positions.emplace_back(std::sin(polar) * std::cos(azimuth),
          std::sin(polar) * std::sin(azimuth), std::cos(polar));
      }
    }
    buffers->positions.emplace_back(0.0F, 0.0F, -1.0F);
    const auto south = static_cast<uint32_t>(buffers->positions.size() - 1U);
    const auto at
      = [segments](const uint32_t ring, const uint32_t segment) -> uint32_t {
      return 1U + ((ring - 1U) * segments) + (segment % segments);
    };
    auto& indices = buffers->indices;
    for (uint32_t segment = 0U; segment < segments; ++segment) {
      indices.insert(
        indices.end(), { 0U, at(1U, segment), at(1U, segment + 1U) });
      indices.insert(indices.end(),
        { south, at(rings - 1U, segment + 1U), at(rings - 1U, segment) });
    }
    for (uint32_t ring = 1U; ring + 1U < rings; ++ring) {
      for (uint32_t segment = 0U; segment < segments; ++segment) {
        indices.insert(indices.end(),
          {
            at(ring, segment),
            at(ring + 1U, segment),
            at(ring + 1U, segment + 1U),
            at(ring, segment),
            at(ring + 1U, segment + 1U),
            at(ring, segment + 1U),
          });
      }
    }
    buffers->ranges = {
      TriangleRange {
        .surface = 0,
        .first_index = 0,
        .index_count = static_cast<uint32_t>(indices.size()),
      },
    };
    return buffers;
  }

  //! A source without normals, given flat ones as glTF requires (smoothing
  //! angle 0), keeps every corner apart at LOD 0, yet simplifies on its own
  //! shared vertices into the requested LODs, each faceted in turn.
  NOLINT_TEST_F(GeometryPipelineSplitTest, FacetedSourceStillGetsItsLods)
  {
    const auto buffers
      = MakeSphereMeshBuffers({ .segments = 32U, .rings = 16U });
    const auto source_vertices = buffers->positions.size();
    auto item = WithGeneratedLods(MakeWorkItem(buffers), 3U);
    item.request.options.normals_smoothing_angle_deg = 0.0F;

    const auto result = Cook(std::move(item));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    EXPECT_FALSE(
      HasDiagnosticCode(result.diagnostics, "import.lod_generation.stopped"));
    const auto& cooked = result.cooked.value();
    ASSERT_EQ(cooked.lods.size(), 4U);
    // Faceted: the corners of a vertex take their faces' normals, so LOD 0
    // has more vertices than the source shares.
    EXPECT_GT(
      cooked.lods.front().vertex_buffer.data.size() / sizeof(data::Vertex),
      source_vertices);
    for (size_t lod = 1U; lod < cooked.lods.size(); ++lod) {
      EXPECT_LE(cooked.lods.at(lod).index_buffer.data.size() * 4U,
        cooked.lods.at(lod - 1U).index_buffer.data.size() * 3U);
    }
  }

  //! Generated smooth normals give the corners around a vertex one normal, so
  //! they weld back into the source's vertices.
  NOLINT_TEST_F(GeometryPipelineSplitTest, SmoothGeneratedNormalsWeld)
  {
    const auto buffers
      = MakeSphereMeshBuffers({ .segments = 32U, .rings = 16U });
    const auto source_vertices = buffers->positions.size();

    const auto result = Cook(MakeWorkItem(buffers));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto views = ReadCookedViews(result.cooked.value());
    EXPECT_EQ(views.vertices.size(), source_vertices);
  }

  //! Two faces folded beyond the smoothing angle keep a hard edge: the corners
  //! on the fold stay two vertices each, one per face.
  NOLINT_TEST_F(GeometryPipelineSplitTest, SharpFoldsKeepSeparateNormals)
  {
    auto buffers = std::make_shared<MeshBuffers>();
    buffers->positions = {
      glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 1.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 1.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, 1.0F },
    };
    buffers->indices = { 0, 1, 2, 0, 3, 1 };
    buffers->ranges = {
      TriangleRange {
        .surface = 0,
        .first_index = 0,
        .index_count = 6,
      },
    };

    const auto result = Cook(MakeWorkItem(buffers));

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked) << "Expected cooked geometry";
    const auto views = ReadCookedViews(result.cooked.value());
    EXPECT_EQ(views.vertices.size(), 6U)
      << "a 90 degree fold beyond the default 60 degrees stays hard";
  }

} // namespace
} // namespace oxygen::content::import::test
