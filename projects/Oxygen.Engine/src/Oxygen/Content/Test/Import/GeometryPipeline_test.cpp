//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

using namespace oxygen::content::import;
using namespace oxygen;
using namespace oxygen::co;
namespace co = co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

constexpr uint32_t kGeomAttr_Normal = 1u << 0u;
constexpr uint32_t kGeomAttr_Tangent = 1u << 1u;
constexpr uint32_t kGeomAttr_Bitangent = 1u << 2u;
constexpr uint32_t kGeomAttr_Texcoord0 = 1u << 3u;
constexpr uint32_t kGeomAttr_Color0 = 1u << 4u;
constexpr uint32_t kGeomAttr_JointWeights = 1u << 5u;
constexpr uint32_t kGeomAttr_JointIndices = 1u << 6u;

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
  return data::AssetKey { .guid = {
                            0x10,
                            0x11,
                            0x12,
                            0x13,
                            0x14,
                            0x15,
                            0x16,
                            0x17,
                            0x18,
                            0x19,
                            0x1A,
                            0x1B,
                            0x1C,
                            0x1D,
                            0x1E,
                            0x1F,
                          } };
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
      .material_slot = 0,
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
  buffers->joint_weights = {
    glm::vec4 { 0.5F, 0.3F, 0.2F, 0.0F },
    glm::vec4 { 0.5F, 0.3F, 0.2F, 0.0F },
    glm::vec4 { 0.5F, 0.3F, 0.2F, 0.0F },
  };
  buffers->inverse_bind_matrices = {
    glm::mat4(1.0F),
    glm::mat4(1.0F),
    glm::mat4(1.0F),
  };
  buffers->joint_remap = { 0, 1, 2 };
  return buffers;
}

[[nodiscard]] auto MakeTriangleMesh(const MeshBuffers& buffers) -> TriangleMesh
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
  return item;
}

[[nodiscard]] auto MakeProceduralWorkItem(std::shared_ptr<MeshBuffers> buffers)
  -> MeshBuildPipeline::WorkItem
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
  return item;
}

[[nodiscard]] auto MakeWorkItemWithLods(std::shared_ptr<MeshBuffers> buffers,
  const uint32_t lod_count) -> MeshBuildPipeline::WorkItem
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

  return item;
}

[[nodiscard]] auto HasDiagnosticCode(
  const std::vector<ImportDiagnostic>& diagnostics, std::string_view code)
  -> bool
{
  return std::any_of(diagnostics.begin(), diagnostics.end(),
    [code](
      const ImportDiagnostic& diagnostic) { return diagnostic.code == code; });
}

template <typename T>
[[nodiscard]] auto ReadStructAt(
  const std::vector<std::byte>& bytes, const size_t offset) -> T
{
  T out {};
  if (bytes.size() < offset + sizeof(T)) {
    return out;
  }
  std::memcpy(&out, bytes.data() + offset, sizeof(T));
  return out;
}

//=== Basic Behavior Tests
//===-----------------------------------------------------//

class GeometryPipelineBasicTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify a simple mesh emits geometry descriptor and buffers.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithSingleTriangle_EmitsCookedPayload)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  EXPECT_TRUE(result.diagnostics.empty());

  const auto& cooked = *result.cooked;
  ASSERT_EQ(cooked.lods.size(), 1u);

  const auto& lod0 = cooked.lods.front();
  EXPECT_TRUE(lod0.auxiliary_buffers.empty());
  EXPECT_EQ(lod0.vertex_buffer.data.size(), sizeof(data::Vertex) * 3u);
  EXPECT_EQ(lod0.index_buffer.data.size(), sizeof(uint32_t) * 3u);

  const auto& bytes = cooked.descriptor_bytes;
  ASSERT_GE(bytes.size(), sizeof(data::pak::GeometryAssetDesc));

  const auto asset_desc = ReadStructAt<data::pak::GeometryAssetDesc>(bytes, 0);
  EXPECT_EQ(asset_desc.header.asset_type,
    static_cast<uint8_t>(data::AssetType::kGeometry));
  EXPECT_EQ(asset_desc.header.version, data::pak::kGeometryAssetVersion);
  EXPECT_EQ(asset_desc.lod_count, 1u);
  EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Normal), 0u);
  EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Tangent), 0u);
  EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Bitangent), 0u);
  EXPECT_NE((asset_desc.header.variant_flags & kGeomAttr_Texcoord0), 0u);
  EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_Color0), 0u);
  EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_JointIndices), 0u);
  EXPECT_EQ((asset_desc.header.variant_flags & kGeomAttr_JointWeights), 0u);

  size_t offset = sizeof(data::pak::GeometryAssetDesc);
  const auto mesh_desc = ReadStructAt<data::pak::MeshDesc>(bytes, offset);
  EXPECT_EQ(mesh_desc.submesh_count, 1u);
  EXPECT_EQ(mesh_desc.mesh_view_count, 1u);
  EXPECT_EQ(
    mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kStandard));

  offset += sizeof(data::pak::MeshDesc);
  const auto submesh_desc = ReadStructAt<data::pak::SubMeshDesc>(bytes, offset);
  EXPECT_EQ(submesh_desc.mesh_view_count, 1u);
  EXPECT_EQ(submesh_desc.material_asset_key, MakeDefaultMaterialKey());

  offset += sizeof(data::pak::SubMeshDesc);
  const auto view_desc = ReadStructAt<data::pak::MeshViewDesc>(bytes, offset);
  EXPECT_EQ(view_desc.first_index, 0u);
  EXPECT_EQ(view_desc.index_count, 3u);
  EXPECT_EQ(view_desc.vertex_count, 3u);
}

//! Verify long mesh/LOD names emit truncation warnings.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithLongNames_EmitsTruncationWarnings)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  auto item = MakeWorkItem(buffers);
  item.mesh_name = std::string(data::pak::kMaxNameSize + 8, 'M');
  item.storage_mesh_name = item.mesh_name;
  item.lods.front().lod_name = std::string(data::pak::kMaxNameSize + 8, 'L');

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(std::move(item));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.cooked.has_value());
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.name_truncated"));
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.lod_name_truncated"));
}

//! Verify skinned mesh descriptors include the skinned mesh blob.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithSkinnedMesh_EmitsSkinnedBlob)
{
  // Arrange
  const auto buffers = MakeSkinnedTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeSkinnedWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.cooked.has_value());
  EXPECT_TRUE(result.diagnostics.empty());

  const auto& cooked = *result.cooked;
  ASSERT_EQ(cooked.lods.size(), 1u);

  const auto& bytes = cooked.descriptor_bytes;
  ASSERT_GE(bytes.size(), sizeof(data::pak::GeometryAssetDesc));

  size_t offset = sizeof(data::pak::GeometryAssetDesc);
  const auto mesh_desc = ReadStructAt<data::pak::MeshDesc>(bytes, offset);
  EXPECT_EQ(
    mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kSkinned));
  EXPECT_EQ(mesh_desc.submesh_count, 1u);
  EXPECT_EQ(mesh_desc.mesh_view_count, 1u);

  offset += sizeof(data::pak::MeshDesc);
  const auto skinned_blob
    = ReadStructAt<data::pak::SkinnedMeshInfo>(bytes, offset);
  EXPECT_EQ(skinned_blob.joint_count, 3u);
  EXPECT_EQ(skinned_blob.influences_per_vertex, 4u);

  offset += sizeof(data::pak::SkinnedMeshInfo);
  const auto submesh_desc = ReadStructAt<data::pak::SubMeshDesc>(bytes, offset);
  EXPECT_EQ(submesh_desc.mesh_view_count, 1u);
  EXPECT_EQ(submesh_desc.material_asset_key, MakeDefaultMaterialKey());

  offset += sizeof(data::pak::SubMeshDesc);
  const auto view_desc = ReadStructAt<data::pak::MeshViewDesc>(bytes, offset);
  EXPECT_EQ(view_desc.first_index, 0u);
  EXPECT_EQ(view_desc.index_count, 3u);
  EXPECT_EQ(view_desc.vertex_count, 3u);
}

//! Verify skinned meshes without inverse bind matrices fail.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_SkinnedMissingInverseBind_ReturnsFailure)
{
  // Arrange
  auto buffers = MakeSkinnedTriangleMeshBuffers();
  buffers->inverse_bind_matrices.clear();

  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeSkinnedWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "mesh.missing_inverse_bind"));
}

//! Verify procedural meshes are rejected with explicit diagnostics.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithProceduralMesh_ReturnsFailure)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeProceduralWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "mesh.procedural_unsupported"));
}

//! Verify descriptor finalization patches buffer indices and content hash.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, FinalizeDescriptor_PatchesIndicesAndHash)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  std::vector<ImportDiagnostic> diagnostics;
  std::optional<std::vector<std::byte>> finalized;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });
    GeometryPipeline finalizer(
      pool, GeometryPipeline::Config { .with_content_hashing = true });

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
        .vertex_buffer = 11,
        .index_buffer = 22,
      };

      finalized = co_await finalizer.FinalizeDescriptorBytes(
        std::span<const MeshBufferBindings>(&bindings, 1),
        result.cooked->descriptor_bytes,
        std::span<const GeometryPipeline::MaterialKeyPatch> {}, diagnostics);
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_TRUE(finalized.has_value());
  ASSERT_TRUE(diagnostics.empty());

  const auto& bytes = *finalized;
  const auto asset_desc = ReadStructAt<data::pak::GeometryAssetDesc>(bytes, 0);
  EXPECT_NE(asset_desc.header.content_hash, 0u);

  size_t offset = sizeof(data::pak::GeometryAssetDesc);
  const auto mesh_desc = ReadStructAt<data::pak::MeshDesc>(bytes, offset);
  EXPECT_EQ(mesh_desc.info.standard.vertex_buffer, 11u);
  EXPECT_EQ(mesh_desc.info.standard.index_buffer, 22u);
}

//! Verify missing positions produce a diagnostic and failure.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithMissingPositions_ReturnsFailure)
{
  // Arrange
  auto buffers = std::make_shared<MeshBuffers>();
  buffers->indices = { 0, 1, 2 };
  buffers->ranges = {
    TriangleRange {
      .material_slot = 0,
      .first_index = 0,
      .index_count = 3,
    },
  };

  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeWorkItem(std::move(buffers)));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.missing_positions"));
}

//! Verify oversized vertex buffers return a diagnostic.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithVertexBufferTooLarge_ReturnsFailure)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
        .max_data_blob_bytes = sizeof(data::Vertex) * 2u,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.buffer_too_large"));
}

//! Verify oversized skinned buffers return a diagnostic.
NOLINT_TEST_F(
  GeometryPipelineBasicTest, Collect_WithSkinnedBufferTooLarge_ReturnsFailure)
{
  // Arrange
  const auto buffers = MakeSkinnedTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
        .max_data_blob_bytes = sizeof(glm::uvec4) * 2u,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeSkinnedWorkItem(buffers));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.buffer_too_large"));
}

//! Verify LOD count above the maximum returns a diagnostic.
NOLINT_TEST_F(GeometryPipelineBasicTest, Collect_WithTooManyLods_ReturnsFailure)
{
  // Arrange
  const auto buffers = MakeTriangleMeshBuffers();
  MeshBuildPipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    MeshBuildPipeline pipeline(pool,
      MeshBuildPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      co_await pipeline.Submit(MakeWorkItemWithLods(buffers, 9));
      result = co_await pipeline.Collect();
      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.cooked.has_value());
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics, "mesh.invalid_lod_count"));
}

} // namespace
