//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_render.h>
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

struct TestResolvedVirtualPage {
  glm::mat4 view_matrix { 1.0F };
  glm::mat4 projection_matrix { 1.0F };
};

auto ResolvedVirtualPageOverlapsBoundingSphere(
  const TestResolvedVirtualPage& page,
  const glm::vec4& world_bounding_sphere) noexcept -> bool
{
  if (world_bounding_sphere.w <= 0.0F) {
    return true;
  }

  const glm::vec4 center_ws(world_bounding_sphere.x, world_bounding_sphere.y,
    world_bounding_sphere.z, 1.0F);
  const glm::vec4 center_ls = page.view_matrix * center_ws;
  const glm::vec4 center_clip = page.projection_matrix * center_ls;
  const float radius = world_bounding_sphere.w;

  const float clip_radius_x = std::abs(page.projection_matrix[0][0]) * radius;
  const float clip_radius_y = std::abs(page.projection_matrix[1][1]) * radius;
  const float clip_radius_z = std::abs(page.projection_matrix[2][2]) * radius;
  constexpr float kClipPadding = 1.0e-3F;

  return center_clip.x + clip_radius_x >= (-1.0F - kClipPadding)
    && center_clip.x - clip_radius_x <= (1.0F + kClipPadding)
    && center_clip.y + clip_radius_y >= (-1.0F - kClipPadding)
    && center_clip.y - clip_radius_y <= (1.0F + kClipPadding)
    && center_clip.z + clip_radius_z >= (0.0F - kClipPadding)
    && center_clip.z - clip_radius_z <= (1.0F + kClipPadding);
}

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

[[nodiscard]] auto MakeAlphaTestMaterial(
  const oxygen::data::MaterialDomain domain
  = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  oxygen::data::pak::render::MaterialAssetDesc desc {};
  constexpr const char* kName = "AlphaTest";
  std::memcpy(desc.header.name, kName, std::strlen(kName));
  desc.header.name[std::strlen(kName)] = '\0';
  desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  desc.material_domain = static_cast<std::uint8_t>(domain);
  desc.flags = oxygen::data::pak::render::kMaterialFlag_AlphaTest;
  desc.base_color[0] = 1.0F;
  desc.base_color[1] = 1.0F;
  desc.base_color[2] = 1.0F;
  desc.base_color[3] = 1.0F;
  desc.alpha_cutoff = oxygen::data::Unorm16 { 0.5F };
  desc.base_color_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.normal_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.metallic_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.roughness_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.ambient_occlusion_texture
    = oxygen::data::pak::core::kFallbackResourceIndex;

  return std::make_shared<const oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey {}, desc);
}

[[nodiscard]] auto MakeMaterial(const oxygen::data::MaterialDomain domain,
  const std::string_view name = "RoutingMaterial")
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  oxygen::data::pak::render::MaterialAssetDesc desc {};
  const auto copy_count
    = (std::min)(name.size(), sizeof(desc.header.name) - 1U);
  std::memcpy(desc.header.name, name.data(), copy_count);
  desc.header.name[copy_count] = '\0';
  desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  desc.material_domain = static_cast<std::uint8_t>(domain);
  desc.base_color[0] = 1.0F;
  desc.base_color[1] = 1.0F;
  desc.base_color[2] = 1.0F;
  desc.base_color[3] = 1.0F;
  desc.base_color_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.normal_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.metallic_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.roughness_texture = oxygen::data::pak::core::kFallbackResourceIndex;
  desc.ambient_occlusion_texture
    = oxygen::data::pak::core::kFallbackResourceIndex;

  return std::make_shared<const oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey {}, desc);
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
  ResetViewData_ClearsPreviousViewEmissionBeforeNextView)
{
  auto multi_view_geometry
    = MakeTwoViewGeometryRef("DrawMetadataEmitter.ResetViewData.MultiView");
  auto single_view_geometry
    = MakeSimpleGeometryRef("DrawMetadataEmitter.ResetViewData.SingleView");
  {
    oxygen::data::AssetKey::ByteArray bytes {};
    bytes[0] = 1U;
    multi_view_geometry.asset_key = oxygen::data::AssetKey::FromBytes(bytes);
  }
  {
    oxygen::data::AssetKey::ByteArray bytes {};
    bytes[0] = 2U;
    single_view_geometry.asset_key = oxygen::data::AssetKey::FromBytes(bytes);
  }

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto multi_handle = GeoUploader().GetOrAllocate(multi_view_geometry);
  const auto single_handle = GeoUploader().GetOrAllocate(single_view_geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  EXPECT_NE(
    GeoUploader().GetShaderVisibleIndices(multi_handle).vertex_srv_index,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_NE(
    GeoUploader().GetShaderVisibleIndices(single_handle).vertex_srv_index,
    oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData first_view_item_a {};
  first_view_item_a.geometry = multi_view_geometry;
  first_view_item_a.submesh_index = 0U;
  first_view_item_a.transform_handle
    = oxygen::engine::sceneprep::TransformHandle {
        oxygen::engine::sceneprep::TransformHandle::Index { 1U },
        oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
      };

  Emitter().EmitDrawMetadata(first_view_item_a);
  Emitter().SortAndPartition();

  EXPECT_EQ(Emitter().GetDrawMetadataBytes().size(),
    2U * sizeof(oxygen::engine::DrawMetadata));
  const auto first_view_srv = Emitter().GetDrawMetadataSrvIndex();
  EXPECT_NE(first_view_srv, oxygen::kInvalidShaderVisibleIndex);

  Emitter().ResetViewData();

  EXPECT_TRUE(Emitter().GetDrawMetadataBytes().empty());
  EXPECT_TRUE(Emitter().GetPartitions().empty());
  EXPECT_TRUE(Emitter().GetDrawBoundingSpheres().empty());
  EXPECT_EQ(
    Emitter().GetInstanceDataSrvIndex(), oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData second_view_item {};
  second_view_item.geometry = single_view_geometry;
  second_view_item.submesh_index = 0U;
  second_view_item.transform_handle
    = oxygen::engine::sceneprep::TransformHandle {
        oxygen::engine::sceneprep::TransformHandle::Index { 2U },
        oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
      };

  Emitter().EmitDrawMetadata(second_view_item);
  Emitter().SortAndPartition();

  EXPECT_EQ(Emitter().GetDrawMetadataBytes().size(),
    sizeof(oxygen::engine::DrawMetadata));
  const auto second_view_srv = Emitter().GetDrawMetadataSrvIndex();
  EXPECT_NE(second_view_srv, oxygen::kInvalidShaderVisibleIndex);
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

NOLINT_TEST_F(DrawMetadataEmitterTest,
  EmitDrawMetadata_ShadowOnlyDrawsStayOutOfMainViewPartitions)
{
  const auto geometry
    = MakeSimpleGeometryRef("DrawMetadataEmitter.ShadowOnlyRouting");

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData main_view_item {};
  main_view_item.geometry = geometry;
  main_view_item.submesh_index = 0U;
  main_view_item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 21U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  main_view_item.cast_shadows = true;
  main_view_item.main_view_visible = true;

  oxygen::engine::sceneprep::RenderItemData shadow_only_item = main_view_item;
  shadow_only_item.transform_handle
    = oxygen::engine::sceneprep::TransformHandle {
        oxygen::engine::sceneprep::TransformHandle::Index { 22U },
        oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
      };
  shadow_only_item.main_view_visible = false;

  Emitter().EmitDrawMetadata(main_view_item);
  Emitter().EmitDrawMetadata(shadow_only_item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), 2U * sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);

  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kMainViewVisible));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(draws[1].flags.IsSet(PassMaskBit::kMainViewVisible));
  EXPECT_TRUE(draws[1].flags.IsSet(PassMaskBit::kShadowCaster));

  const auto partitions = Emitter().GetPartitions();
  ASSERT_EQ(partitions.size(), 2U);
  EXPECT_FALSE(partitions[0].pass_mask.IsSet(PassMaskBit::kMainViewVisible));
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(partitions[1].pass_mask.IsSet(PassMaskBit::kMainViewVisible));
  EXPECT_TRUE(partitions[1].pass_mask.IsSet(PassMaskBit::kShadowCaster));
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  EmitDrawMetadata_AlphaTestFlagRoutesOpaqueDomainMaterialThroughMaskedShadowPath)
{
  namespace d = oxygen::data;

  std::vector<d::Vertex> vertices(3);
  vertices[0].position = { -1.0F, 0.0F, 0.0F };
  vertices[1].position = { 1.0F, 0.0F, 0.0F };
  vertices[2].position = { 0.0F, 1.0F, 0.0F };

  const std::vector<std::uint32_t> indices { 0U, 1U, 2U };
  const auto material = MakeAlphaTestMaterial(d::MaterialDomain::kOpaque);

  auto mesh = d::MeshBuilder(0U, "DrawMetadataEmitter.AlphaTestRouting")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("default", material)
                .WithMeshView({ .first_index = 0U,
                  .index_count = 3U,
                  .first_vertex = 0U,
                  .vertex_count = 3U })
                .EndSubMesh()
                .Build();

  const auto geometry = oxygen::engine::sceneprep::GeometryRef {
    .asset_key = oxygen::data::AssetKey {},
    .lod_index = 0U,
    .mesh = std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh)),
  };

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices_srv = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices_srv.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices_srv.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item {};
  item.geometry = geometry;
  item.submesh_index = 0U;
  item.material = oxygen::engine::sceneprep::MaterialRef {
    .source_asset_key = material->GetAssetKey(),
    .resolved_asset_key = material->GetAssetKey(),
    .resolved_asset = material,
  };
  item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 41U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item.cast_shadows = true;

  Emitter().EmitDrawMetadata(item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kMasked));
  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kOpaque));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kShadowCaster));

  const auto partitions = Emitter().GetPartitions();
  ASSERT_EQ(partitions.size(), 1U);
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kMasked));
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kShadowCaster));
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  EmitDrawMetadata_OpaqueMaterialStaysOutOfTransparentPartitions)
{
  namespace d = oxygen::data;

  std::vector<d::Vertex> vertices(3);
  vertices[0].position = { -1.0F, 0.0F, 0.0F };
  vertices[1].position = { 1.0F, 0.0F, 0.0F };
  vertices[2].position = { 0.0F, 1.0F, 0.0F };

  const std::vector<std::uint32_t> indices { 0U, 1U, 2U };
  const auto material
    = MakeMaterial(d::MaterialDomain::kOpaque, "OpaqueRouting");

  auto mesh = d::MeshBuilder(0U, "DrawMetadataEmitter.OpaqueRouting")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("default", material)
                .WithMeshView({ .first_index = 0U,
                  .index_count = 3U,
                  .first_vertex = 0U,
                  .vertex_count = 3U })
                .EndSubMesh()
                .Build();

  const auto geometry = oxygen::engine::sceneprep::GeometryRef {
    .asset_key = oxygen::data::AssetKey {},
    .lod_index = 0U,
    .mesh = std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh)),
  };

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices_srv = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices_srv.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices_srv.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item {};
  item.geometry = geometry;
  item.submesh_index = 0U;
  item.material = oxygen::engine::sceneprep::MaterialRef {
    .source_asset_key = material->GetAssetKey(),
    .resolved_asset_key = material->GetAssetKey(),
    .resolved_asset = material,
  };
  item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 51U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item.main_view_visible = true;

  Emitter().EmitDrawMetadata(item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kOpaque));
  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kTransparent));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kMainViewVisible));

  const auto partitions = Emitter().GetPartitions();
  ASSERT_EQ(partitions.size(), 1U);
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kOpaque));
  EXPECT_FALSE(partitions[0].pass_mask.IsSet(PassMaskBit::kTransparent));
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kMainViewVisible));
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  EmitDrawMetadata_AlphaBlendedMaterialRoutesOnlyThroughTransparentPartition)
{
  namespace d = oxygen::data;

  std::vector<d::Vertex> vertices(3);
  vertices[0].position = { -1.0F, 0.0F, 0.0F };
  vertices[1].position = { 1.0F, 0.0F, 0.0F };
  vertices[2].position = { 0.0F, 1.0F, 0.0F };

  const std::vector<std::uint32_t> indices { 0U, 1U, 2U };
  const auto material
    = MakeMaterial(d::MaterialDomain::kAlphaBlended, "TransparentRouting");

  auto mesh = d::MeshBuilder(0U, "DrawMetadataEmitter.TransparentRouting")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("default", material)
                .WithMeshView({ .first_index = 0U,
                  .index_count = 3U,
                  .first_vertex = 0U,
                  .vertex_count = 3U })
                .EndSubMesh()
                .Build();

  const auto geometry = oxygen::engine::sceneprep::GeometryRef {
    .asset_key = oxygen::data::AssetKey {},
    .lod_index = 0U,
    .mesh = std::shared_ptr<const oxygen::data::Mesh>(std::move(mesh)),
  };

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices_srv = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices_srv.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices_srv.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item {};
  item.geometry = geometry;
  item.submesh_index = 0U;
  item.material = oxygen::engine::sceneprep::MaterialRef {
    .source_asset_key = material->GetAssetKey(),
    .resolved_asset_key = material->GetAssetKey(),
    .resolved_asset = material,
  };
  item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 61U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item.main_view_visible = true;
  item.cast_shadows = true;

  Emitter().EmitDrawMetadata(item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), sizeof(oxygen::engine::DrawMetadata));

  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);
  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kOpaque));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kTransparent));
  EXPECT_FALSE(draws[0].flags.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(draws[0].flags.IsSet(PassMaskBit::kMainViewVisible));

  const auto partitions = Emitter().GetPartitions();
  ASSERT_EQ(partitions.size(), 1U);
  EXPECT_FALSE(partitions[0].pass_mask.IsSet(PassMaskBit::kOpaque));
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kTransparent));
  EXPECT_FALSE(partitions[0].pass_mask.IsSet(PassMaskBit::kShadowCaster));
  EXPECT_TRUE(partitions[0].pass_mask.IsSet(PassMaskBit::kMainViewVisible));
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  SortAndPartition_ReordersBoundingSpheresWithShadowCasterDrawOrder)
{
  const auto geometry
    = MakeSimpleGeometryRef("DrawMetadataEmitter.BoundingSphereOrdering");

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData main_view_item {};
  main_view_item.geometry = geometry;
  main_view_item.submesh_index = 0U;
  main_view_item.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 31U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  main_view_item.cast_shadows = true;
  main_view_item.main_view_visible = true;
  main_view_item.world_bounding_sphere = glm::vec4(10.0F, 0.0F, 0.0F, 1.0F);

  oxygen::engine::sceneprep::RenderItemData shadow_only_item = main_view_item;
  shadow_only_item.transform_handle
    = oxygen::engine::sceneprep::TransformHandle {
        oxygen::engine::sceneprep::TransformHandle::Index { 32U },
        oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
      };
  shadow_only_item.main_view_visible = false;
  shadow_only_item.world_bounding_sphere = glm::vec4(-20.0F, 1.0F, 0.0F, 2.0F);

  Emitter().EmitDrawMetadata(main_view_item);
  Emitter().EmitDrawMetadata(shadow_only_item);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), 2U * sizeof(oxygen::engine::DrawMetadata));
  const auto* draws
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draws, nullptr);

  const auto draw_bounds = Emitter().GetDrawBoundingSpheres();
  ASSERT_EQ(draw_bounds.size(), 2U);

  EXPECT_EQ(draws[0].transform_index, 32U);
  EXPECT_EQ(draws[1].transform_index, 31U);
  EXPECT_EQ(draw_bounds[0], shadow_only_item.world_bounding_sphere);
  EXPECT_EQ(draw_bounds[1], main_view_item.world_bounding_sphere);
}

NOLINT_TEST_F(DrawMetadataEmitterTest,
  ApplyInstancingBatches_MergesBoundingSpheresConservatively)
{
  const auto geometry
    = MakeSimpleGeometryRef("DrawMetadataEmitter.BatchedBoundingSphere");

  BeginFrame(SequenceNumber { 1U }, Slot { 0U });
  const auto geo_handle = GeoUploader().GetOrAllocate(geometry);
  GeoUploader().EnsureFrameResources();

  BeginFrame(SequenceNumber { 2U }, Slot { 1U });
  const auto indices = GeoUploader().GetShaderVisibleIndices(geo_handle);
  ASSERT_NE(indices.vertex_srv_index, oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(indices.index_srv_index, oxygen::kInvalidShaderVisibleIndex);

  oxygen::engine::sceneprep::RenderItemData item_a {};
  item_a.geometry = geometry;
  item_a.submesh_index = 0U;
  item_a.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 41U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item_a.cast_shadows = true;
  item_a.world_bounding_sphere = glm::vec4(-5.0F, 0.0F, 0.0F, 1.0F);

  oxygen::engine::sceneprep::RenderItemData item_b = item_a;
  item_b.transform_handle = oxygen::engine::sceneprep::TransformHandle {
    oxygen::engine::sceneprep::TransformHandle::Index { 42U },
    oxygen::engine::sceneprep::TransformHandle::Generation { 1U },
  };
  item_b.world_bounding_sphere = glm::vec4(7.0F, 0.0F, 0.0F, 2.0F);

  Emitter().EmitDrawMetadata(item_a);
  Emitter().EmitDrawMetadata(item_b);
  Emitter().SortAndPartition();

  const auto bytes = Emitter().GetDrawMetadataBytes();
  ASSERT_EQ(bytes.size(), sizeof(oxygen::engine::DrawMetadata));
  const auto* draw
    = reinterpret_cast<const oxygen::engine::DrawMetadata*>(bytes.data());
  ASSERT_NE(draw, nullptr);
  EXPECT_EQ(draw[0].instance_count, 2U);

  const auto draw_bounds = Emitter().GetDrawBoundingSpheres();
  ASSERT_EQ(draw_bounds.size(), 1U);

  const auto& merged = draw_bounds[0];
  EXPECT_GT(merged.w, 0.0F);
  for (const auto source :
    { item_a.world_bounding_sphere, item_b.world_bounding_sphere }) {
    const auto center_delta
      = glm::distance(glm::vec3(merged), glm::vec3(source));
    EXPECT_LE(center_delta + source.w, merged.w + 1.0e-4F);
  }
}

NOLINT_TEST(DrawMetadataEmitterStandaloneTest,
  ShadowRasterCulling_ConservativelyKeepsTouchingCasterBounds)
{
  TestResolvedVirtualPage page {};
  page.view_matrix = glm::mat4(1.0F);
  page.projection_matrix
    = glm::orthoRH_ZO(-1.0F, 1.0F, -1.0F, 1.0F, 0.1F, 10.0F);

  const glm::vec4 inside_sphere(0.0F, 0.0F, -1.0F, 0.25F);
  const glm::vec4 touching_sphere(1.2F, 0.0F, -1.0F, 0.25F);
  const glm::vec4 outside_sphere(2.0F, 0.0F, -1.0F, 0.25F);
  const glm::vec4 invalid_sphere(0.0F, 0.0F, 0.0F, 0.0F);

  EXPECT_TRUE(ResolvedVirtualPageOverlapsBoundingSphere(page, inside_sphere));
  EXPECT_TRUE(ResolvedVirtualPageOverlapsBoundingSphere(page, touching_sphere));
  EXPECT_FALSE(ResolvedVirtualPageOverlapsBoundingSphere(page, outside_sphere));
  EXPECT_TRUE(ResolvedVirtualPageOverlapsBoundingSphere(page, invalid_sphere));
}

} // namespace
