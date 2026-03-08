//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/ScenePrep/GeometryRef.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/GeometryUploaderTest.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

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

namespace {

using oxygen::observer_ptr;
using oxygen::engine::PassMaskBit;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::resources::DrawMetadataEmitter;
using oxygen::renderer::resources::GeometryUploader;
using oxygen::renderer::testing::FakeAssetLoader;
using oxygen::renderer::testing::FakeGraphics;

[[nodiscard]] auto MakeSimpleGeometryRef(std::string_view mesh_name)
  -> oxygen::engine::sceneprep::GeometryRef
{
  namespace d = oxygen::data;

  std::vector<d::Vertex> vertices(3);
  vertices[0].position = { -1.0F, 0.0F, 0.0F };
  vertices[1].position = { 1.0F, 0.0F, 0.0F };
  vertices[2].position = { 0.0F, 1.0F, 0.0F };

  const std::vector<std::uint32_t> indices { 0U, 1U, 2U };
  const auto material = d::MaterialAsset::CreateDefault();

  auto mesh = d::MeshBuilder(0U, mesh_name)
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("default", material)
                .WithMeshView({ .first_index = 0U,
                  .index_count = 3U,
                  .first_vertex = 0U,
                  .vertex_count = 3U })
                .EndSubMesh()
                .Build();

  return oxygen::engine::sceneprep::GeometryRef {
    .asset_key = oxygen::data::AssetKey {},
    .lod_index = 0U,
    .mesh = std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh)),
  };
}

[[nodiscard]] auto MakeTwoViewGeometryRef(std::string_view mesh_name)
  -> oxygen::engine::sceneprep::GeometryRef
{
  namespace d = oxygen::data;

  std::vector<d::Vertex> vertices(6);
  vertices[0].position = { -1.0F, 0.0F, 0.0F };
  vertices[1].position = { 0.0F, 1.0F, 0.0F };
  vertices[2].position = { 1.0F, 0.0F, 0.0F };
  vertices[3].position = { -1.0F, -1.0F, 0.0F };
  vertices[4].position = { 0.0F, 0.0F, 0.0F };
  vertices[5].position = { 1.0F, -1.0F, 0.0F };

  const std::vector<std::uint32_t> indices { 0U, 1U, 2U, 3U, 4U, 5U };
  const auto material = d::MaterialAsset::CreateDefault();

  auto mesh = d::MeshBuilder(0U, mesh_name)
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("two-views", material)
                .WithMeshView({ .first_index = 0U,
                  .index_count = 3U,
                  .first_vertex = 0U,
                  .vertex_count = 3U })
                .WithMeshView({ .first_index = 3U,
                  .index_count = 3U,
                  .first_vertex = 3U,
                  .vertex_count = 3U })
                .EndSubMesh()
                .Build();

  return oxygen::engine::sceneprep::GeometryRef {
    .asset_key = oxygen::data::AssetKey {},
    .lod_index = 0U,
    .mesh = std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh)),
  };
}

class DrawMetadataEmitterTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr { gfx_.get() }, DefaultUploadPolicy());
    staging_provider_
      = uploader_->CreateRingBufferStaging(oxygen::frame::SlotCount { 2 }, 4U);
    inline_transfers_ = std::make_unique<InlineTransfersCoordinator>(
      observer_ptr { gfx_.get() });
    asset_loader_ = std::make_unique<FakeAssetLoader>();
    geometry_uploader_ = std::make_unique<GeometryUploader>(
      observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { asset_loader_.get() });
    emitter_ = std::make_unique<DrawMetadataEmitter>(
      observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
      observer_ptr { geometry_uploader_.get() },
      observer_ptr<oxygen::renderer::resources::MaterialBinder> { nullptr },
      observer_ptr { inline_transfers_.get() });
  }

  auto BeginFrame(const SequenceNumber sequence, const Slot slot) -> void
  {
    uploader_->OnFrameStart(RendererTagFactory::Get(), slot);
    inline_transfers_->OnFrameStart(RendererTagFactory::Get(), slot);
    geometry_uploader_->OnFrameStart(RendererTagFactory::Get(), slot);
    emitter_->OnFrameStart(RendererTagFactory::Get(), sequence, slot);
  }

  [[nodiscard]] auto Emitter() const noexcept -> DrawMetadataEmitter&
  {
    return *emitter_;
  }

  [[nodiscard]] auto GeoUploader() const noexcept -> GeometryUploader&
  {
    return *geometry_uploader_;
  }

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::unique_ptr<FakeAssetLoader> asset_loader_;
  std::unique_ptr<GeometryUploader> geometry_uploader_;
  std::unique_ptr<DrawMetadataEmitter> emitter_;
};

NOLINT_TEST_F(
  DrawMetadataEmitterTest, GetDrawMetadataSrvIndexLazilyEnsuresFrameResources)
{
  const auto geometry = MakeSimpleGeometryRef("DrawMetadataEmitter.LazySrv");

  // Prime geometry residency so emitter does not skip the draw.
  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  // Advance frame to publish retired upload results.
  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item {};
  item.geometry = geometry;
  item.submesh_index = 0U;
  item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 1U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item.sort_distance2 = 42.0F; // NOLINT

  Emitter().EmitDrawMetadata(item);
  ASSERT_FALSE(Emitter().GetDrawMetadataBytes().empty());

  const auto srv0 = Emitter().GetDrawMetadataSrvIndex();
  const auto srv1 = Emitter().GetDrawMetadataSrvIndex();

  EXPECT_NE(srv0, oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(srv0, srv1);
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  SortAndPartition_EqualSortingKeysRemainDeterministicByEmitOrder)
{
  const auto geometry
    = MakeTwoViewGeometryRef("DrawMetadataEmitter.DeterministicOrdering");

  // Prime geometry residency so emitter does not skip draws.
  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item {};
  item.geometry = geometry;
  item.submesh_index = 0U;
  item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 7U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item.sort_distance2 = 10.0F;

  Emitter().EmitDrawMetadata(item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), 2U * sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);

  // Sorting keys are equal for both draws; order should remain the emit
  // order.
  EXPECT_EQ(draws[0].first_index, 0U);
  EXPECT_EQ(draws[1].first_index, 3U);
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  EmitDrawMetadata_ShadowCasterRoutingFollowsRenderItemShadowFlag)
{
  const auto geometry
    = MakeSimpleGeometryRef("DrawMetadataEmitter.ShadowCasterRouting");

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData shadowed_item {};
  shadowed_item.geometry = geometry;
  shadowed_item.submesh_index = 0U;
  shadowed_item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 11U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  shadowed_item.cast_shadows = true;

  oxygen::engine::sceneprep::RenderItemData unshadowed_item = shadowed_item;
  unshadowed_item.transform_handle
    = oxygen::engine::sceneprep::TransformHandle {
        oxygen::engine::sceneprep::TransformHandle::Index { 12U },
        oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
      };
  unshadowed_item.cast_shadows = false;

  Emitter().EmitDrawMetadata(shadowed_item);
  Emitter().EmitDrawMetadata(unshadowed_item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), 2U * sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);

  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(draws[1].flags.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kOpaque));
  EXPECT_TRUE(draws[1].flags.IsSet(PassMaskBit::kOpaque));

  const auto partitions = Emitter().GetPartitions();
  ASSERT_EQ(partitions.size(), 2U);
  EXPECT_FALSE(partitions[0].pass_mask.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(partitions[1].pass_mask.IsSet(PassMaskBit::kShadowCaster));
}

} // namespace
