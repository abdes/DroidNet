//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>

using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::Vertex;

// Mock Mesh for MeshView construction (temporary until helper removal)
class MockMesh : public Mesh {
public:
  MockMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
    : Mesh(0, vertices, indices)
    , vertices_(vertices)
    , indices_(indices)
  {
  }

  MOCK_METHOD(
    (std::span<const Vertex>), Vertices, (), (const, noexcept, override));

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
};

//! Fixture for basic MeshView construction and accessor scenarios
class MeshViewBasicTest : public ::testing::Test {
protected:
  void SetupMockMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
  {
    mesh_ = std::make_unique<MockMesh>(vertices, indices);
    ON_CALL(*mesh_, Vertices())
      .WillByDefault(
        ::testing::Return(std::span<const Vertex>(mesh_->vertices_)));
  }
  std::unique_ptr<MockMesh> mesh_;
};

//! Fixture for death/error boundary validation scenarios
class MeshViewDeathTest : public ::testing::Test {
protected:
  void SetupMockMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
  {
    mesh_ = std::make_unique<MockMesh>(vertices, indices);
    ON_CALL(*mesh_, Vertices())
      .WillByDefault(
        ::testing::Return(std::span<const Vertex>(mesh_->vertices_)));
  }
  std::unique_ptr<MockMesh> mesh_;
};

//! Fixture for index type widening / promotion behavior (edge / format cases)
class MeshViewIndexTypeTest : public ::testing::Test { };

namespace {

using ::testing::AllOf;
using ::testing::SizeIs;

//! Tests MeshView construction with valid data and accessor methods.
NOLINT_TEST_F(MeshViewBasicTest, ConstructAndAccess)
{
  // Arrange
  std::vector<Vertex> vertices = {
    { .position = { 0, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 1, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 0, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 1, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices()).Times(::testing::AnyNumber());
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));

  // Act
  MeshView view(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 6,
      .first_vertex = 0,
      .vertex_count = 4,
    });

  // Assert
  EXPECT_THAT(view.Vertices(), SizeIs(4));
  EXPECT_EQ(view.IndexBuffer().Count(), 6u);
  EXPECT_EQ(view.Vertices().data(), vertices.data());
  {
    auto view_indices = view.IndexBuffer().AsU32();
    ASSERT_EQ(view_indices.size(), indices.size());
    for (size_t i = 0; i < indices.size(); ++i) {
      EXPECT_EQ(view_indices[i], indices[i]);
    }
  }
}

//! Real mesh (non-mock) construction & access path (basic scenario).
NOLINT_TEST(MeshViewBasicRealMeshTest, RealMesh_ViewValidity)
{
  // Arrange
  auto material = oxygen::data::MaterialAsset::CreateDefault();
  std::vector<Vertex> vertices = {
    { .position = { 0, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 1, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 0, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  auto mesh = oxygen::data::MeshBuilder(0, "triangle")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("main", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();
  ASSERT_NE(mesh, nullptr);

  // Act
  MeshView mesh_view(*mesh,
    oxygen::data::pak::MeshViewDesc { .first_index = 0,
      .index_count = 3,
      .first_vertex = 0,
      .vertex_count = 3 });

  // Assert
  EXPECT_THAT(mesh_view.Vertices(), SizeIs(3));
  EXPECT_EQ(mesh_view.IndexBuffer().Count(), 3u);
  EXPECT_EQ(mesh_view.Vertices().data(), mesh->Vertices().data());
  EXPECT_EQ(
    mesh_view.IndexBuffer().AsU32().data(), mesh->IndexBuffer().AsU32().data());
}

//! Out-of-bounds view creation (death scenarios consolidated from old file).
NOLINT_TEST_F(MeshViewDeathTest, OutOfBoundsCreation_Death)
{
  // Arrange
  auto material = oxygen::data::MaterialAsset::CreateDefault();
  std::vector<Vertex> vertices = {
    { .position = { 0, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 1, 0, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
    { .position = { 0, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  auto mesh = oxygen::data::MeshBuilder(0, "triangle")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("main", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();
  ASSERT_NE(mesh, nullptr);

  // Act & Assert
  EXPECT_DEATH((MeshView { *mesh,
                 oxygen::data::pak::MeshViewDesc { .first_index = 0,
                   .index_count = 3,
                   .first_vertex = 10,
                   .vertex_count = 3 } }),
    "");
  EXPECT_DEATH((MeshView { *mesh,
                 oxygen::data::pak::MeshViewDesc { .first_index = 10,
                   .index_count = 3,
                   .first_vertex = 0,
                   .vertex_count = 3 } }),
    "");
  EXPECT_DEATH((MeshView { *mesh,
                 oxygen::data::pak::MeshViewDesc { .first_index = 0,
                   .index_count = 3,
                   .first_vertex = 5,
                   .vertex_count = 1 } }),
    "");
  EXPECT_DEATH((MeshView { *mesh,
                 oxygen::data::pak::MeshViewDesc { .first_index = 0,
                   .index_count = 1,
                   .first_vertex = 3,
                   .vertex_count = 5 } }),
    "");
}

//! Tests MeshView handles empty vertex and index data (should be a death test).
NOLINT_TEST_F(MeshViewDeathTest, Empty)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices;
  SetupMockMesh(vertices, indices);

  // Act & Assert
  EXPECT_DEATH(MeshView mesh_view(*mesh_,
                 oxygen::data::pak::MeshViewDesc {
                   .first_index = 0,
                   .index_count = 0,
                   .first_vertex = 0,
                   .vertex_count = 0,
                 }),
    "");
}

//! Tests MeshView copy and move semantics work correctly.
NOLINT_TEST_F(MeshViewBasicTest, CopyMove)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices { 0, 1 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));

  MeshView mesh_view1(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 2,
      .first_vertex = 0,
      .vertex_count = 2,
    });

  // Act
  MeshView mesh_view2 = mesh_view1;
  MeshView mesh_view3 = std::move(mesh_view1);

  // Assert
  EXPECT_THAT(mesh_view2.Vertices(), SizeIs(2));
  EXPECT_EQ(mesh_view2.IndexBuffer().Count(), 2u);
  EXPECT_THAT(mesh_view3.Vertices(), SizeIs(2));
  EXPECT_EQ(mesh_view3.IndexBuffer().Count(), 2u);
}

//! (6) Death: zero index_count but positive vertex_count should fail
NOLINT_TEST_F(MeshViewDeathTest, ZeroIndexCountPositiveVertexCount_Death)
{
  // Arrange
  std::vector<Vertex> vertices(3);
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));

  // Act & Assert
  EXPECT_DEATH((MeshView { *mesh_,
                 oxygen::data::pak::MeshViewDesc {
                   .first_index = 0,
                   .index_count = 0, // invalid
                   .first_vertex = 0,
                   .vertex_count = 3, // valid
                 } }),
    "at least one index");
}

//! (7) Death: zero vertex_count but positive index_count should fail
NOLINT_TEST_F(MeshViewDeathTest, ZeroVertexCountPositiveIndexCount_Death)
{
  // Arrange
  std::vector<Vertex> vertices(3);
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));

  // Act & Assert
  EXPECT_DEATH((MeshView { *mesh_,
                 oxygen::data::pak::MeshViewDesc {
                   .first_index = 0,
                   .index_count = 3, // valid
                   .first_vertex = 0,
                   .vertex_count = 0, // invalid
                 } }),
    "at least one vertex");
}

//! (8) Death: last index past end (off-by-one) should fail (boundary)
NOLINT_TEST_F(MeshViewDeathTest, EdgeOutOfRange_LastIndexPastEnd_Death)
{
  // Arrange
  std::vector<Vertex> vertices(4);
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));

  // Sanity: a valid slice touching the end should succeed
  NOLINT_EXPECT_NO_THROW((MeshView { *mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = static_cast<uint32_t>(indices.size()),
      .first_vertex = 0,
      .vertex_count = static_cast<uint32_t>(vertices.size()),
    } }));

  // Act & Assert: one past end should death
  EXPECT_DEATH(
    (MeshView { *mesh_,
      oxygen::data::pak::MeshViewDesc {
        .first_index = 1, // shift by 1
        .index_count = static_cast<uint32_t>(indices.size()), // now overflows
        .first_vertex = 0,
        .vertex_count = static_cast<uint32_t>(vertices.size()),
      } }),
    "index range exceeds");
}

//! (9) 16-bit indices: Widened() iteration yields same sequence as source
NOLINT_TEST_F(MeshViewIndexTypeTest, SixteenBitIndices_WidenedIterationMatches)
{
  // Arrange
  // Build a referenced-storage mesh manually: we only need index bytes & format
  // Reuse MockMesh path: construct Mesh with 32-bit indices then adapt? Instead
  // build a small mesh builder with referenced buffers (preferred path).
  // We'll construct a standalone Mesh with 16-bit indices using BufferResource.
  using oxygen::data::BufferResource;
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  namespace pak = oxygen::data::pak;

  std::vector<Vertex> vertices = { Vertex {}, Vertex {}, Vertex {}, Vertex {} };
  std::vector<std::uint16_t> u16_indices { 0, 1, 2, 2, 3, 0 };

  // Vertex buffer desc (structured: stride = sizeof(Vertex), format=0)
  pak::BufferResourceDesc vertex_desc { .data_offset = 0,
    .size_bytes
    = static_cast<pak::DataBlobSizeT>(vertices.size() * sizeof(Vertex)),
    .usage_flags = 0x01, // VertexBuffer
    .element_stride = sizeof(Vertex),
    .element_format = 0,
    .reserved = {} };
  std::vector<uint8_t> vertex_bytes(vertices.size() * sizeof(Vertex));
  std::memcpy(vertex_bytes.data(), vertices.data(), vertex_bytes.size());
  auto vbuf
    = std::make_shared<BufferResource>(vertex_desc, std::move(vertex_bytes));

  // Index buffer desc: element_format = kR16UInt, stride inferred by format
  pak::BufferResourceDesc index_desc { .data_offset = 0,
    .size_bytes
    = static_cast<pak::DataBlobSizeT>(u16_indices.size() * sizeof(uint16_t)),
    .usage_flags = 0x02, // IndexBuffer
    .element_stride = 0, // unused because format specifies size
    .element_format = static_cast<uint8_t>(oxygen::Format::kR16UInt),
    .reserved = {} };
  std::vector<uint8_t> index_bytes(u16_indices.size() * sizeof(uint16_t));
  std::memcpy(index_bytes.data(), u16_indices.data(), index_bytes.size());
  auto ibuf
    = std::make_shared<BufferResource>(index_desc, std::move(index_bytes));

  auto material = MaterialAsset::CreateDefault();
  MeshBuilder builder;
  auto mesh = builder.WithBufferResources(vbuf, ibuf)
                .BeginSubMesh("m", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = static_cast<uint32_t>(u16_indices.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()) })
                .EndSubMesh()
                .Build();
  ASSERT_NE(mesh, nullptr);
  ASSERT_EQ(mesh->IndexCount(), u16_indices.size());

  auto view = mesh->SubMeshes()[0].MeshViews()[0];

  // Act
  std::vector<uint32_t> widened;
  for (auto v : view.IndexBuffer().Widened()) {
    widened.push_back(v);
  }

  // Assert
  ASSERT_EQ(widened.size(), u16_indices.size());
  for (size_t i = 0; i < u16_indices.size(); ++i) {
    EXPECT_EQ(widened[i], static_cast<uint32_t>(u16_indices[i]));
  }
}

} // namespace
