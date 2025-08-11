//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::BufferResource;
using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;

namespace {

namespace pak = oxygen::data::pak;

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::SizeIs;

//=== Test Fixtures ===-------------------------------------------------------//

//! Test fixture for basic MeshBuilder functionality and storage management
class MeshBuilderBasicTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Create sample vertices and indices for owned storage tests
    vertices_ = {
      {
        .position = { 0.0f, 0.0f, 0.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { 0.0f, 0.0f },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = {},
        .color = {},
      },
      {
        .position = { 1.0f, 0.0f, 0.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { 1.0f, 0.0f },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = {},
        .color = {},
      },
      {
        .position = { 0.5f, 1.0f, 0.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { 0.5f, 1.0f },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = {},
        .color = {},
      },
    };

    indices_ = { 0, 1, 2 };

    // Create mock buffer resources for referenced storage tests
    // Create descriptor for vertex buffer
    pak::BufferResourceDesc vertex_desc = { .data_offset = 0,
      .size_bytes
      = static_cast<pak::DataBlobSizeT>(vertices_.size() * sizeof(Vertex)),
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
      .element_stride = sizeof(Vertex),
      .element_format = 0, // Raw buffer
      .reserved = {} };

    // Create vertex data vector
    std::vector<uint8_t> vertex_data(vertices_.size() * sizeof(Vertex));
    std::memcpy(vertex_data.data(), vertices_.data(), vertex_data.size());

    vertex_buffer_ = std::make_shared<BufferResource>(
      std::move(vertex_desc), std::move(vertex_data));

    // Create descriptor for index buffer
    pak::BufferResourceDesc index_desc = { .data_offset = 0,
      .size_bytes = static_cast<pak::DataBlobSizeT>(
        indices_.size() * sizeof(std::uint32_t)),
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer),
      .element_stride = sizeof(std::uint32_t),
      .element_format = 0, // Raw buffer
      .reserved = {} };

    // Create index data vector
    std::vector<uint8_t> index_data(indices_.size() * sizeof(std::uint32_t));
    std::memcpy(index_data.data(), indices_.data(), index_data.size());

    index_buffer_ = std::make_shared<BufferResource>(std::move(index_desc),
      std::move(index_data)); // Create a mock material asset
    material_ = MaterialAsset::CreateDefault();
  }

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::shared_ptr<BufferResource> vertex_buffer_;
  std::shared_ptr<BufferResource> index_buffer_;
  std::shared_ptr<const MaterialAsset> material_;
};

//! Test fixture for error scenarios and storage validation
class MeshBuilderErrorTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Create minimal test data
    vertices_ = { {
      .position = { 0.0f, 0.0f, 0.0f },
      .normal = { 0.0f, 1.0f, 0.0f },
      .texcoord = { 0.0f, 0.0f },
      .tangent = { 1.0f, 0.0f, 0.0f },
      .bitangent = {},
      .color = {},
    } };

    indices_ = { 0 };

    // Create mock buffer resources for referenced storage tests
    // Create descriptor for vertex buffer
    pak::BufferResourceDesc vertex_desc = { .data_offset = 0,
      .size_bytes
      = static_cast<pak::DataBlobSizeT>(vertices_.size() * sizeof(Vertex)),
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
      .element_stride = sizeof(Vertex),
      .element_format = 0, // Raw buffer
      .reserved = {} };

    // Create vertex data vector
    std::vector<uint8_t> vertex_data(vertices_.size() * sizeof(Vertex));
    std::memcpy(vertex_data.data(), vertices_.data(), vertex_data.size());

    vertex_buffer_ = std::make_shared<BufferResource>(
      std::move(vertex_desc), std::move(vertex_data));

    // Create descriptor for index buffer
    pak::BufferResourceDesc index_desc = { .data_offset = 0,
      .size_bytes = static_cast<pak::DataBlobSizeT>(
        indices_.size() * sizeof(std::uint32_t)),
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer),
      .element_stride = sizeof(std::uint32_t),
      .element_format = 0, // Raw buffer
      .reserved = {} };

    // Create index data vector
    std::vector<uint8_t> index_data(indices_.size() * sizeof(std::uint32_t));
    std::memcpy(index_data.data(), indices_.data(), index_data.size());

    index_buffer_ = std::make_shared<BufferResource>(
      std::move(index_desc), std::move(index_data));

    material_ = MaterialAsset::CreateDefault();
  }

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::shared_ptr<BufferResource> vertex_buffer_;
  std::shared_ptr<BufferResource> index_buffer_;
  std::shared_ptr<const MaterialAsset> material_;
};

//! Test fixture for MeshBuilder death / invariant enforcement scenarios
class MeshBuilderDeathTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Provide a small but non-trivial owned storage set
    vertices_ = { {
                    .position = { 0.0f, 0.0f, 0.0f },
                    .normal = { 0.0f, 1.0f, 0.0f },
                    .texcoord = { 0.0f, 0.0f },
                    .tangent = { 1.0f, 0.0f, 0.0f },
                    .bitangent = {},
                    .color = {},
                  },
      {
        .position = { 1.0f, 0.0f, 0.0f },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { 1.0f, 0.0f },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = {},
        .color = {},
      } };
    indices_ = { 0, 1 };
    material_ = MaterialAsset::CreateDefault();

    // Valid referenced storage buffers for tests needing them
    pak::BufferResourceDesc vertex_desc = { .data_offset = 0,
      .size_bytes
      = static_cast<pak::DataBlobSizeT>(vertices_.size() * sizeof(Vertex)),
      .usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer),
      .element_stride = sizeof(Vertex),
      .element_format = 0,
      .reserved = {} };
    std::vector<uint8_t> vbytes(vertices_.size() * sizeof(Vertex));
    std::memcpy(vbytes.data(), vertices_.data(), vbytes.size());
    vertex_buffer_
      = std::make_shared<BufferResource>(vertex_desc, std::move(vbytes));
  }

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::shared_ptr<BufferResource> vertex_buffer_; // for referenced path
  std::shared_ptr<const MaterialAsset> material_;
};

//=== Storage Type Query Tests ===--------------------------------------------//

//! Tests that a new MeshBuilder starts in uninitialized storage state
NOLINT_TEST_F(MeshBuilderBasicTest, InitialState_StorageUninitialized)
{
  // Arrange
  MeshBuilder builder;

  // Act & Assert
  EXPECT_TRUE(builder.IsStorageUninitialized());
  EXPECT_FALSE(builder.IsUsingOwnedStorage());
  EXPECT_FALSE(builder.IsUsingReferencedStorage());
}

//! Tests that WithVertices transitions to owned storage
NOLINT_TEST_F(MeshBuilderBasicTest, WithVertices_TransitionsToOwnedStorage)
{
  // Arrange
  MeshBuilder builder;

  // Act
  builder.WithVertices(vertices_);

  // Assert
  EXPECT_FALSE(builder.IsStorageUninitialized());
  EXPECT_TRUE(builder.IsUsingOwnedStorage());
  EXPECT_FALSE(builder.IsUsingReferencedStorage());
}

//! Tests that WithIndices transitions to owned storage
NOLINT_TEST_F(MeshBuilderBasicTest, WithIndices_TransitionsToOwnedStorage)
{
  // Arrange
  MeshBuilder builder;

  // Act
  builder.WithIndices(indices_);

  // Assert
  EXPECT_FALSE(builder.IsStorageUninitialized());
  EXPECT_TRUE(builder.IsUsingOwnedStorage());
  EXPECT_FALSE(builder.IsUsingReferencedStorage());
}

//! Tests that WithBufferResources transitions to referenced storage
NOLINT_TEST_F(
  MeshBuilderBasicTest, WithBufferResources_TransitionsToReferencedStorage)
{
  // Arrange
  MeshBuilder builder;

  // Act
  builder.WithBufferResources(vertex_buffer_, index_buffer_);

  // Assert
  EXPECT_FALSE(builder.IsStorageUninitialized());
  EXPECT_FALSE(builder.IsUsingOwnedStorage());
  EXPECT_TRUE(builder.IsUsingReferencedStorage());
}

//=== Storage Type Validation Tests ===---------------------------------------//

//! Tests that owned storage methods can be called together without error
NOLINT_TEST_F(MeshBuilderBasicTest, OwnedStorageMethods_CanBeMixed)
{
  // Arrange
  MeshBuilder builder;

  // Act & Assert - no exceptions should be thrown
  NOLINT_EXPECT_NO_THROW(
    { builder.WithVertices(vertices_).WithIndices(indices_); });

  EXPECT_TRUE(builder.IsUsingOwnedStorage());
}

//! Tests that WithBufferResources after WithVertices throws logic_error
NOLINT_TEST_F(
  MeshBuilderErrorTest, WithBufferResources_AfterWithVertices_ThrowsLogicError)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { builder.WithBufferResources(vertex_buffer_, index_buffer_); },
    std::logic_error);
}

//! Tests that WithBufferResources after WithIndices throws logic_error
NOLINT_TEST_F(
  MeshBuilderErrorTest, WithBufferResources_AfterWithIndices_ThrowsLogicError)
{
  // Arrange
  MeshBuilder builder;
  builder.WithIndices(indices_);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { builder.WithBufferResources(vertex_buffer_, index_buffer_); },
    std::logic_error);
}

//! Tests that WithVertices after WithBufferResources throws logic_error
NOLINT_TEST_F(
  MeshBuilderErrorTest, WithVertices_AfterWithBufferResources_ThrowsLogicError)
{
  // Arrange
  MeshBuilder builder;
  builder.WithBufferResources(vertex_buffer_, index_buffer_);

  // Act & Assert
  NOLINT_EXPECT_THROW({ builder.WithVertices(vertices_); }, std::logic_error);
}

//! Tests that WithIndices after WithBufferResources throws logic_error
NOLINT_TEST_F(
  MeshBuilderErrorTest, WithIndices_AfterWithBufferResources_ThrowsLogicError)
{
  // Arrange
  MeshBuilder builder;
  builder.WithBufferResources(vertex_buffer_, index_buffer_);

  // Act & Assert
  NOLINT_EXPECT_THROW({ builder.WithIndices(indices_); }, std::logic_error);
}

//=== Error Message Quality Tests ===-----------------------------------------//

//! Tests that error message is descriptive when mixing owned and referenced
//! storage
NOLINT_TEST_F(
  MeshBuilderErrorTest, StorageValidation_ProvidesDescriptiveErrorMessage)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_);

  // Act & Assert
  try {
    builder.WithBufferResources(vertex_buffer_, index_buffer_);
    FAIL() << "Expected std::logic_error to be thrown";
  } catch (const std::logic_error& e) {
    EXPECT_THAT(e.what(),
      AllOf(HasSubstr("Cannot mix storage types"), HasSubstr("owned storage"),
        HasSubstr("referenced storage")));
  }
}

//! Tests that error message mentions the correct current and requested storage
//! types
NOLINT_TEST_F(MeshBuilderErrorTest,
  StorageValidation_MentionsCorrectStorageTypes_ReferencedThenOwned)
{
  // Arrange
  MeshBuilder builder;
  builder.WithBufferResources(vertex_buffer_, index_buffer_);

  // Act & Assert
  try {
    builder.WithVertices(vertices_);
    FAIL() << "Expected std::logic_error to be thrown";
  } catch (const std::logic_error& e) {
    EXPECT_THAT(e.what(),
      AllOf(HasSubstr("referenced storage (WithBufferResources)"),
        HasSubstr("owned storage (WithVertices/WithIndices)")));
  }
}

//! (18) Ensures calling BeginSubMesh twice without ending the first throws.
NOLINT_TEST_F(MeshBuilderErrorTest, DuplicateBeginSubMeshWithoutEnd_Throws)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);

  // Act
  auto first = builder.BeginSubMesh("first", material_);

  // Assert
  NOLINT_EXPECT_THROW(
    { (void)builder.BeginSubMesh("second", material_); }, std::logic_error);
  (void)first; // suppress unused (keeps first alive for clarity)
}

//! (19) Ensures calling EndSubMesh when no submesh is in progress throws.
NOLINT_TEST_F(MeshBuilderErrorTest, EndSubMeshWithoutBegin_Throws)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);

  // Act & Assert: construct an orphan SubMeshBuilder directly (unsupported)
  oxygen::data::SubMeshBuilder orphan(builder, "orphan", material_);
  NOLINT_EXPECT_THROW(
    { builder.EndSubMesh(std::move(orphan)); }, std::logic_error);
}

//! (6 / 19) Build while a SubMesh is still active should death with message
//! mentioning 'active SubMesh'.
NOLINT_TEST(MeshBuilderErrorMessageTest, BuildWithActiveSubMesh_MentionsActive)
{
  // Arrange
  MeshBuilder builder;
  std::vector<Vertex> vertices = { { .position = { 0, 0, 0 },
                                     .normal = {},
                                     .texcoord = {},
                                     .tangent = {},
                                     .bitangent = {},
                                     .color = {} },
    { .position = { 1, 0, 0 },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} } };
  std::vector<uint32_t> indices { 0, 1 };
  auto material = MaterialAsset::CreateDefault();
  builder.WithVertices(vertices).WithIndices(indices);
  auto sm = builder.BeginSubMesh("sm", material);
  sm.WithMeshView({ .first_index = 0,
    .index_count = 2,
    .first_vertex = 0,
    .vertex_count = 2 });

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _ = builder.Build(), "active SubMesh");
}

//! (16) Reusing SubMeshBuilder after EndSubMesh should throw logic_error.
NOLINT_TEST_F(MeshBuilderErrorTest, SubMeshBuilderReuseAfterEnd_Throws)
{
  // Arrange
  MeshBuilder builder;
  std::vector<Vertex> vertices = { { .position = { 0, 0, 0 },
                                     .normal = {},
                                     .texcoord = {},
                                     .tangent = {},
                                     .bitangent = {},
                                     .color = {} },
    { .position = { 1, 0, 0 },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} } };
  std::vector<uint32_t> indices { 0, 1 };
  auto material = MaterialAsset::CreateDefault();
  builder.WithVertices(vertices).WithIndices(indices);
  auto submesh_builder = builder.BeginSubMesh("sm", material);
  submesh_builder.WithMeshView({ .first_index = 0,
    .index_count = 2,
    .first_vertex = 0,
    .vertex_count = 2 });
  builder.EndSubMesh(std::move(submesh_builder));

  // Act & Assert: attempt to reuse ended builder (undefined behavior -> throw)
  NOLINT_EXPECT_THROW(
    { builder.EndSubMesh(std::move(submesh_builder)); }, std::logic_error);
}

//! (17) Double EndSubMesh (calling EndSubMesh twice on same active builder)
//! should throw logic_error.
NOLINT_TEST_F(MeshBuilderErrorTest, SubMeshBuilderDoubleEnd_Throws)
{
  // Arrange
  MeshBuilder builder;
  std::vector<Vertex> vertices = { { .position = { 0, 0, 0 },
                                     .normal = {},
                                     .texcoord = {},
                                     .tangent = {},
                                     .bitangent = {},
                                     .color = {} },
    { .position = { 1, 0, 0 },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} } };
  std::vector<uint32_t> indices { 0, 1 };
  auto material = MaterialAsset::CreateDefault();
  builder.WithVertices(vertices).WithIndices(indices);
  auto submesh_builder = builder.BeginSubMesh("sm", material);
  submesh_builder.WithMeshView({ .first_index = 0,
    .index_count = 2,
    .first_vertex = 0,
    .vertex_count = 2 });
  builder.EndSubMesh(std::move(submesh_builder));

  // Act & Assert: second EndSubMesh on already-ended builder
  NOLINT_EXPECT_THROW(
    { builder.EndSubMesh(std::move(submesh_builder)); }, std::logic_error);
}

//! (33) Ensures BeginSubMesh rejects null material pointer.
NOLINT_TEST_F(MeshBuilderErrorTest, BeginSubMeshNullMaterial_Throws)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { (void)builder.BeginSubMesh("null_mat", nullptr); }, std::logic_error);
}

//! (32) Ensures attempting to change storage type after starting a SubMesh
//! (before EndSubMesh) throws a logic_error to prevent inconsistent state.
NOLINT_TEST_F(
  MeshBuilderErrorTest, WithBufferResources_AfterBeginSubMesh_Throws)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);
  auto submesh_builder = builder.BeginSubMesh("in_progress", material_);

  // Act & Assert: attempting to switch to referenced storage mid-submesh
  NOLINT_EXPECT_THROW(
    { builder.WithBufferResources(vertex_buffer_, index_buffer_); },
    std::logic_error);

  // Cleanup (not reached if exception message incorrect, but keep pattern)
  (void)submesh_builder; // keep alive for clarity
}

//=== Successful Build Tests ===----------------------------------------------//

//! Tests that owned storage mesh can be built successfully
NOLINT_TEST_F(MeshBuilderBasicTest, Build_WithOwnedStorage_Succeeds)
{
  // Arrange
  MeshBuilder builder;

  // Act
  auto mesh = builder.WithVertices(vertices_)
                .WithIndices(indices_)
                .BeginSubMesh("test", material_)
                .WithMeshView({
                  .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3,
                })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->VertexCount(), 3);
  EXPECT_EQ(mesh->IndexCount(), 3);
  EXPECT_THAT(mesh->SubMeshes(), SizeIs(1));
}

//! Tests that referenced storage mesh can be built successfully
NOLINT_TEST_F(MeshBuilderBasicTest, Build_WithReferencedStorage_Succeeds)
{
  // Arrange
  MeshBuilder builder;

  // Act
  auto mesh = builder.WithBufferResources(vertex_buffer_, index_buffer_)
                .BeginSubMesh("test", material_)
                .WithMeshView({
                  .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3,
                })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->VertexCount(), 3);
  EXPECT_EQ(mesh->IndexCount(), 3);
  EXPECT_THAT(mesh->SubMeshes(), SizeIs(1));
}

//! (33a) Tests that referenced storage mesh can be built without an index
//! buffer (constructor comment says optional). MeshView still requires at
//! least one index logically, so we provide index_count=1; resulting Mesh has
//! zero actual indices (IndexBuffer empty) and IsIndexed()==false.
NOLINT_TEST_F(
  MeshBuilderBasicTest, Build_WithReferencedStorage_NoIndexBuffer_Succeeds)
{
  // Arrange
  // Create a vertex buffer only; intentionally omit index buffer
  MeshBuilder builder;
  auto vertex_only_builder = MeshBuilder();
  // Reuse existing vertex_buffer_ from fixture but pass nullptr for index
  // buffer

  // Act
  auto mesh
    = builder.WithBufferResources(vertex_buffer_, nullptr)
        .BeginSubMesh("vertex_only", material_)
        .WithMeshView({
          .first_index = 0,
          .index_count = 1, // Satisfy MeshView invariant although no buffer
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(vertices_.size()),
        })
        .EndSubMesh()
        .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->VertexCount(), vertices_.size());
  EXPECT_EQ(mesh->IndexCount(), 0u);
  EXPECT_FALSE(mesh->IsIndexed());
  EXPECT_THAT(mesh->SubMeshes(), SizeIs(1));
}

//=== Unsuccessful Build Tests (Death) ===------------------------------------//

//! (1) Ensures Build() fails when no submeshes were added.
NOLINT_TEST_F(MeshBuilderDeathTest, BuildWithoutSubMesh_Death)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _ = builder.Build(),
    "Mesh must have at least one submesh");
}

//! (2) Ensures Build() fails when no storage was set (uninitialized storage).
NOLINT_TEST_F(MeshBuilderDeathTest, BuildWithoutStorage_Death)
{
  // Arrange
  MeshBuilder builder;
  builder.BeginSubMesh("test", material_)
    .WithMeshView({ .first_index = 0,
      .index_count = 1,
      .first_vertex = 0,
      .vertex_count = 1 })
    .EndSubMesh();

  // Act & Assert
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = builder.Build(), "Mesh must have vertices");
}

//! (3) Clarifies behavior: indices-only then Build should death (no vertices).
NOLINT_TEST_F(MeshBuilderDeathTest, IndicesOnlyThenBuild_Death)
{
  // Arrange
  MeshBuilder builder;
  builder.WithIndices(indices_)
    .BeginSubMesh("test", material_)
    .WithMeshView({ .first_index = 0,
      .index_count = 1,
      .first_vertex = 0,
      .vertex_count = 1 }) // vertex_count placeholder
    .EndSubMesh();

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _ = builder.Build(), ".*");
}

//! (4) Vertices-only Build now invalid: must supply indices for owned storage.
NOLINT_TEST_F(MeshBuilderDeathTest, VerticesOnlyThenBuild_Death)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_)
    .BeginSubMesh("test", material_)
    .WithMeshView({ .first_index = 0,
      .index_count = 0, // Intentionally zero
      .first_vertex = 0,
      .vertex_count = static_cast<uint32_t>(vertices_.size()) })
    .EndSubMesh();

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _ = builder.Build(), ".*");
}

//! (10) Ensures EndSubMesh() without any prior WithMeshView call throws.
NOLINT_TEST_F(MeshBuilderDeathTest, BuilderAddsSubMeshWithNoViews_Throws)
{
  // Arrange
  MeshBuilder builder;
  builder.WithVertices(vertices_).WithIndices(indices_);

  // Act & Assert: EndSubMesh should throw logic_error before Build phase
  NOLINT_EXPECT_THROW(
    { builder.BeginSubMesh("invalid", material_).EndSubMesh(); },
    std::logic_error);
}

} // namespace
