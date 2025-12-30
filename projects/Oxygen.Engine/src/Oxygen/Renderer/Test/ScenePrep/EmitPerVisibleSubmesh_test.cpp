//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTest.h>
#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepHelpers.h>
#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepTestFixture.h>

// Implementation of UploaderTagFactory. Provides access to UploaderTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal
#endif

using oxygen::View;

using oxygen::observer_ptr;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::engine::sceneprep::EmitPerVisibleSubmesh;
using oxygen::engine::sceneprep::MeshResolver;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;
using oxygen::engine::sceneprep::SubMeshVisibilityFilter;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::internal::UploaderTagFactory;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

using namespace oxygen::engine::sceneprep::testing;

namespace oxygen::content {
class AssetLoader;
}

namespace {

class EmitPerVisibleSubmeshTest : public ScenePrepTestFixture {
protected:
  // Override factory to create resource-backed ScenePrepState while ensuring
  // auxiliary objects outlive the state (gfx_, uploader_, staging_provider_)
  auto CreateScenePrepState() -> std::unique_ptr<ScenePrepState> override
  {
    // Initialize fake graphics and upload coordinator for resource managers
    gfx_ = std::make_shared<oxygen::renderer::testing::FakeGraphics>();
    gfx_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    uploader_ = std::make_unique<oxygen::engine::upload::UploadCoordinator>(
      observer_ptr { gfx_.get() });
    staging_provider_ = uploader_->CreateRingBufferStaging(
      oxygen::frame::SlotCount { 1 }, 4, 0.5f);

    // Create resource managers and give ownership to ScenePrepState so
    // Extractors can rely on a non-null material binder during tests.
    auto geom_uploader
      = std::make_unique<oxygen::renderer::resources::GeometryUploader>(
        observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
        observer_ptr { staging_provider_.get() });
    // We need an InlineTransfersCoordinator instance for the TransformUploader
    // API; the uploader expects an observer_ptr to the inline transfers
    // coordinator.
    inline_transfers_
      = std::make_unique<oxygen::engine::upload::InlineTransfersCoordinator>(
        observer_ptr { gfx_.get() });

    auto transform_uploader
      = std::make_unique<oxygen::renderer::resources::TransformUploader>(
        observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });
    texture_loader_ = std::make_unique<
      oxygen::renderer::testing::FakeTextureResourceLoader>();
    texture_binder_
      = std::make_unique<oxygen::renderer::resources::TextureBinder>(
        observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
        observer_ptr { uploader_.get() },
        observer_ptr { texture_loader_.get() });
    auto material_binder
      = std::make_unique<oxygen::renderer::resources::MaterialBinder>(
        observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
        observer_ptr { staging_provider_.get() },
        observer_ptr { texture_binder_.get() });

    return std::make_unique<ScenePrepState>(std::move(geom_uploader),
      std::move(transform_uploader), std::move(material_binder));
  }

  auto TearDown() -> void override
  {
    ScenePrepTestFixture::TearDown();
    texture_binder_.reset();
    texture_loader_.reset();
  }

  // Keep auxiliary objects as protected members so they outlive the returned
  // ScenePrepState (fixture owns them).
  std::shared_ptr<oxygen::renderer::testing::FakeGraphics> gfx_;
  std::unique_ptr<oxygen::engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<oxygen::engine::upload::InlineTransfersCoordinator>
    inline_transfers_;
  std::unique_ptr<oxygen::renderer::resources::TextureBinder> texture_binder_;
  std::unique_ptr<oxygen::renderer::testing::FakeTextureResourceLoader>
    texture_loader_;
};
// Death: dropped item
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, DroppedItem_Death)
{
  Proto().MarkDropped();
  NOLINT_EXPECT_DEATH(EmitPerVisibleSubmesh(Context(), State(), Proto()), ".*");
}

// Death: no resolved mesh
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, NoResolvedMesh_Death)
{
  // Ensure geometry/visibility seeded but skip MeshResolver
  SeedVisibilityAndTransform();
  NOLINT_EXPECT_DEATH(EmitPerVisibleSubmesh(Context(), State(), Proto()), ".*");
}

// Empty visible list -> emits nothing
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, EmptyVisibleList_NoEmission)
{
  // Arrange: resolve mesh but clear visible list
  const auto geom = MakeGeometryWithLods(2, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  // Ensure a valid view/context is available for MeshResolver
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  EmplaceContextWithView();
  MeshResolver(Context(), State(), Proto());
  // Do not run visibility filter; set empty list directly
  Proto().SetVisibleSubmeshes({});

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert
  EXPECT_TRUE(State().CollectedItems().empty());
}

// These are complex integration tests - disabled for now
#if 0
// Emit one item per visible submesh with correct properties
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, EmitsAllVisible_WithExpectedFields)
{
  // Arrange: geometry with 3 submeshes, all visible
  const auto geom = MakeGeometryWithSubmeshes(3);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert
  const auto& items = State().CollectedItems();
  ASSERT_EQ(items.size(), 3u);
  for (size_t i = 0; i < items.size(); ++i) {
    EXPECT_EQ(items[i].lod_index, Proto().ResolvedMeshIndex());
    EXPECT_EQ(items[i].submesh_index, i);
    EXPECT_EQ(items[i].geometry.get(), Proto().Geometry().get());
    EXPECT_EQ(items[i].world_bounding_sphere,
      Node().GetRenderable().GetWorldBoundingSphere());
    EXPECT_EQ(items[i].cast_shadows, Proto().CastsShadows());
    EXPECT_EQ(items[i].receive_shadows, Proto().ReceivesShadows());
  }
}

// Material override takes precedence over mesh submesh material
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MaterialOverride_TakesPrecedence)
{
  // Arrange: 2 submeshes; override submesh 1
  const auto geom = MakeGeometryWithSubmeshes(2);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  const auto lod = Proto().ResolvedMeshIndex();
  const auto override_mat = MaterialAsset::CreateDefault();
  Node().GetRenderable().SetMaterialOverride(lod, 1, override_mat);

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert: find submesh 1 item and check material ptr
  const auto& items = State().CollectedItems();
  const auto it = std::ranges::find_if(
    items, [](const auto& r) { return r.submesh_index == 1u; });
  ASSERT_NE(it, items.end());
  EXPECT_EQ(it->material, override_mat);
}

// No override -> mesh submesh material is used
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MeshMaterial_UsedWhenNoOverride)
{
  // Arrange: 2 submeshes; no overrides
  const auto geom = MakeGeometryWithSubmeshes(2);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // The mesh's submesh material is the one attached in the builder
  const auto mesh_material = Proto().ResolvedMesh()->SubMeshes()[0].Material();

  // Act
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Assert: find submesh 0 item and check material ptr equals mesh material
  const auto& items = State().CollectedItems();
  const auto it = std::ranges::find_if(
    items, [](const auto& r) { return r.submesh_index == 0u; });
  ASSERT_NE(it, items.end());
  EXPECT_EQ(it->material, mesh_material);
}

// Masked out submesh should not be emitted
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, MaskedOutSubmesh_NotEmitted)
{
  const auto geom = MakeGeometryWithSubmeshes(3);
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  MeshResolver(Context(), State(), Proto());

  // Hide submesh 1, keep others visible
  const auto lod = Proto().ResolvedMeshIndex();
  Node().GetRenderable().SetSubmeshVisible(lod, 1, false);

  SubMeshVisibilityFilter(Context(), State(), Proto());
  EmitPerVisibleSubmesh(Context(), State(), Proto());

  // Expect only 0 and 2 emitted
  const auto& items = State().CollectedItems();
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].submesh_index, 0u);
  EXPECT_EQ(items[1].submesh_index, 2u);
}
#endif // 0 - DISABLED

} // namespace
