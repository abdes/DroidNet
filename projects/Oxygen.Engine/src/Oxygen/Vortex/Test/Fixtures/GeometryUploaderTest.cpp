//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <glm/glm.hpp>

#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Test/Fixtures/GeometryUploaderTest.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

namespace oxygen::vortex::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace oxygen::vortex::testing {

auto GeometryUploaderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<vortex::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, vortex::upload::DefaultUploadPolicy());

  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 2 }, 4);

  default_material_ = DefaultMaterial();

  asset_loader_ = std::make_unique<FakeAssetLoader>();

  geo_uploader_ = std::make_unique<resources::GeometryUploader>(
    observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() },
    observer_ptr { asset_loader_.get() });
}

auto GeometryUploaderTest::TearDown() -> void
{
  // Destroy producer-side owners first so any deferred release registrations
  // they perform happen while Graphics is still alive.
  geo_uploader_.reset();
  staging_provider_.reset();
  uploader_.reset();
  asset_loader_.reset();

  if (gfx_) {
    gfx_->Flush();
    gfx_->GetDeferredReclaimer().OnRendererShutdown();
  }

  gfx_.reset();
}

auto GeometryUploaderTest::GfxPtr() const -> observer_ptr<Graphics>
{
  return observer_ptr<Graphics>(gfx_.get());
}

auto GeometryUploaderTest::Uploader() const
  -> vortex::upload::UploadCoordinator&
{
  return *uploader_;
}

auto GeometryUploaderTest::Staging() const -> vortex::upload::StagingProvider&
{
  return *staging_provider_;
}

auto GeometryUploaderTest::GeoUploader() const -> resources::GeometryUploader&
{
  return *geo_uploader_;
}

auto GeometryUploaderTest::Loader() const -> FakeAssetLoader&
{
  return *asset_loader_;
}

auto GeometryUploaderTest::BeginFrame(frame::Slot slot) -> void
{
  uploader_->OnFrameStart(vortex::internal::RendererTagFactory::Get(), slot);
  geo_uploader_->OnFrameStart(
    vortex::internal::RendererTagFactory::Get(), slot);
}

auto GeometryUploaderTest::DefaultMaterial() const
  -> std::shared_ptr<const data::MaterialAsset>
{
  data::pak::render::MaterialAssetDesc desc {};
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

  data::pak::geometry::MeshViewDesc view_desc {
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

  data::pak::geometry::MeshViewDesc view_desc {
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

} // namespace oxygen::vortex::testing
