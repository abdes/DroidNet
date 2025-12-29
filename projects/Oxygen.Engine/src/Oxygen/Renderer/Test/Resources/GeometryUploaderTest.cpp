//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <glm/glm.hpp>

#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

#ifdef OXYGEN_ENGINE_TESTING

namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

#endif // OXYGEN_ENGINE_TESTING

namespace oxygen::renderer::testing {

auto GeometryUploaderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<engine::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, engine::upload::DefaultUploadPolicy());

  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 2 }, 4);

  default_material_ = DefaultMaterial();

  geo_uploader_ = std::make_unique<resources::GeometryUploader>(
    observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() });
}

auto GeometryUploaderTest::GfxPtr() const -> observer_ptr<Graphics>
{
  return observer_ptr<Graphics>(gfx_.get());
}

auto GeometryUploaderTest::Uploader() const
  -> engine::upload::UploadCoordinator&
{
  return *uploader_;
}

auto GeometryUploaderTest::Staging() const -> engine::upload::StagingProvider&
{
  return *staging_provider_;
}

auto GeometryUploaderTest::GeoUploader() const -> resources::GeometryUploader&
{
  return *geo_uploader_;
}

auto GeometryUploaderTest::BeginFrame(frame::Slot slot) -> void
{
  uploader_->OnFrameStart(renderer::internal::RendererTagFactory::Get(), slot);
  geo_uploader_->OnFrameStart(
    renderer::internal::RendererTagFactory::Get(), slot);
}

auto GeometryUploaderTest::DefaultMaterial() const
  -> std::shared_ptr<const data::MaterialAsset>
{
  data::pak::MaterialAssetDesc desc {};
  return std::make_shared<const data::MaterialAsset>(data::AssetKey {}, desc);
}

auto GeometryUploaderTest::MakeValidTriangleMesh(std::string_view name,
  const bool indexed) const -> std::shared_ptr<const data::Mesh>
{
  const auto vertices = std::vector<data::Vertex> {
    data::Vertex {
      .position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    data::Vertex {
      .position = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 1.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    data::Vertex {
      .position = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 1.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
  };

  const auto indices = std::vector<std::uint32_t> { 0U, 1U, 2U };

  auto builder = data::MeshBuilder(0, name).WithVertices(vertices);
  if (indexed) {
    builder.WithIndices(indices);
  }

  data::pak::MeshViewDesc view_desc {
    .first_index = 0,
    // MeshView enforces index_count > 0 even if the mesh has no index buffer.
    // For non-indexed meshes (no indices provided), keep a non-zero draw range
    // so MeshView construction succeeds; IndexBufferView will be empty.
    .index_count = 3U,
    .first_vertex = 0,
    .vertex_count = 3U,
  };

  auto mesh = builder.BeginSubMesh("default", default_material_)
                .WithMeshView(view_desc)
                .EndSubMesh()
                .Build();

  return std::shared_ptr<const data::Mesh>(std::move(mesh));
}

auto GeometryUploaderTest::MakeInvalidMesh_NoVertices(
  std::string_view name) const -> std::shared_ptr<const data::Mesh>
{
  // Mesh/MeshBuilder enforce non-empty vertex buffers at construction time.
  // Use a non-finite vertex field to produce an invalid mesh instance which
  // still respects Mesh invariants.
  return MakeInvalidMesh_NonFiniteVertex(name);
}

auto GeometryUploaderTest::MakeInvalidMesh_NonFiniteVertex(
  std::string_view name) const -> std::shared_ptr<const data::Mesh>
{
  auto vertices = std::vector<data::Vertex> {
    data::Vertex {
      .position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    data::Vertex {
      .position = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 1.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    data::Vertex {
      .position = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 1.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
  };

  // Introduce invalid data.
  vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();

  const auto indices = std::vector<std::uint32_t> { 0U, 1U, 2U };

  auto builder
    = data::MeshBuilder(0, name).WithVertices(vertices).WithIndices(indices);

  data::pak::MeshViewDesc view_desc {
    .first_index = 0,
    .index_count = 3U,
    .first_vertex = 0,
    .vertex_count = 3U,
  };

  auto mesh = builder.BeginSubMesh("default", default_material_)
                .WithMeshView(view_desc)
                .EndSubMesh()
                .Build();

  return std::shared_ptr<const data::Mesh>(std::move(mesh));
}

} // namespace oxygen::renderer::testing
