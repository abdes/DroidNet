//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadError;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::resources::GeometryUploader;
using oxygen::renderer::testing::GeometryUploaderTest;

[[nodiscard]] auto MakeValidTriangleMesh(std::string_view name, bool indexed)
  -> std::shared_ptr<const oxygen::data::Mesh>
{
  const auto vertices = std::vector<oxygen::data::Vertex> {
    oxygen::data::Vertex {
      .position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    oxygen::data::Vertex {
      .position = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 1.0F, 0.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    oxygen::data::Vertex {
      .position = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .normal = glm::vec3 { 0.0F, 0.0F, 1.0F },
      .texcoord = glm::vec2 { 0.0F, 1.0F },
      .tangent = glm::vec3 { 1.0F, 0.0F, 0.0F },
      .bitangent = glm::vec3 { 0.0F, 1.0F, 0.0F },
      .color = glm::vec4 { 1.0F, 1.0F, 1.0F, 1.0F },
    },
  };

  const auto indices = std::vector<std::uint32_t> { 0U, 1U, 2U };
  auto builder = oxygen::data::MeshBuilder(0, name).WithVertices(vertices);
  if (indexed) {
    builder.WithIndices(indices);
  }

  oxygen::data::pak::MeshViewDesc view_desc {
    .first_index = 0,
    // MeshView enforces index_count > 0 even if the mesh has no index buffer.
    // For non-indexed meshes (no indices provided), keep a non-zero draw range
    // so MeshView construction succeeds; IndexBufferView will be empty.
    .index_count = 3U,
    .first_vertex = 0,
    .vertex_count = 3U,
  };

  auto mesh
    = builder
        .BeginSubMesh("default", oxygen::data::MaterialAsset::CreateDefault())
        .WithMeshView(view_desc)
        .EndSubMesh()
        .Build();

  return std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh));
}

//! GetShaderVisibleIndices must return invalid indices while not resident.
NOLINT_TEST_F(
  GeometryUploaderTest, NotResidentGetShaderVisibleIndicesReturnsInvalidIndices)
{
  // Arrange
  auto& uploader = GeoUploader();
  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = uploader.GetOrAllocate(geometry);

  // Act: do not call EnsureFrameResources explicitly.
  const auto indices = uploader.GetShaderVisibleIndices(handle);

  // Assert
  EXPECT_EQ(indices.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_GT(uploader.GetPendingUploadCount(), 0U);
}

//=== Failure injection (via failing staging provider) ----------------------//

class AlwaysFailStagingProvider final : public StagingProvider {
public:
  explicit AlwaysFailStagingProvider(oxygen::engine::upload::UploaderTag tag)
    : StagingProvider(tag)
  {
  }

  auto Allocate(oxygen::engine::upload::SizeBytes, std::string_view)
    -> std::expected<Allocation, UploadError> override
  {
    return std::unexpected(UploadError::kStagingAllocFailed);
  }

  auto RetireCompleted(oxygen::engine::upload::UploaderTag,
    oxygen::engine::upload::FenceValue) -> void override
  {
  }
};

//! If upload submission fails, indices remain invalid and code does not crash.
NOLINT_TEST(GeometryUploaderFailuresStandaloneTest,
  UploadSubmissionFailureIndicesRemainInvalidAndNoCrash)
{
  // Arrange
  auto gfx = std::make_shared<oxygen::renderer::testing::FakeGraphics>();
  gfx->CreateCommandQueues(SingleQueueStrategy());

  auto upload_coordinator
    = std::make_unique<oxygen::engine::upload::UploadCoordinator>(
      oxygen::observer_ptr { gfx.get() },
      oxygen::engine::upload::DefaultUploadPolicy());

  auto staging = std::make_shared<AlwaysFailStagingProvider>(
    oxygen::engine::upload::internal::UploaderTagFactory::Get());

  auto geo_uploader
    = std::make_unique<GeometryUploader>(oxygen::observer_ptr { gfx.get() },
      oxygen::observer_ptr { upload_coordinator.get() },
      oxygen::observer_ptr { staging.get() });

  upload_coordinator->OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  geo_uploader->OnFrameStart(RendererTagFactory::Get(), Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);

  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = geo_uploader->GetOrAllocate(geometry);

  // Act
  geo_uploader->EnsureFrameResources();
  const auto indices = geo_uploader->GetShaderVisibleIndices(handle);

  // Assert
  EXPECT_EQ(indices.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices.index_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(geo_uploader->GetPendingUploadCount(), 0U);
}

//! TicketNotFound during completion is treated as terminal; indices stay
//! invalid and next Ensure retries without crashing.
NOLINT_TEST_F(
  GeometryUploaderTest, UploadCompletionFailureIndicesRemainInvalidAndNoCrash)
{
  // Arrange
  auto& upload_coordinator = Uploader();
  auto& uploader = GeoUploader();

  BeginFrame(Slot { 0 });

  const auto mesh = MakeValidTriangleMesh("Tri", true);
  const oxygen::data::AssetKey asset_key {
    .guid = oxygen::data::GenerateAssetGuid(),
  };
  const oxygen::engine::sceneprep::GeometryRef geometry {
    .asset_key = asset_key,
    .lod_index = 0U,
    .mesh = mesh,
  };

  const auto handle = uploader.GetOrAllocate(geometry);

  uploader.EnsureFrameResources();
  ASSERT_GT(uploader.GetPendingUploadCount(), 0U);

  // Act: re-enter the same slot so UploadTracker erases the tickets.
  upload_coordinator.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });
  uploader.OnFrameStart(RendererTagFactory::Get(), Slot { 0 });

  const auto indices_0 = uploader.GetShaderVisibleIndices(handle);

  // Assert: still not resident.
  EXPECT_EQ(indices_0.vertex_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(indices_0.index_srv_index, kInvalidShaderVisibleIndex);

  // Act: Ensure again should retry scheduling.
  uploader.EnsureFrameResources();

  // Assert: retry did not crash and produced new pending work.
  EXPECT_GT(uploader.GetPendingUploadCount(), 0U);
}

} // namespace
