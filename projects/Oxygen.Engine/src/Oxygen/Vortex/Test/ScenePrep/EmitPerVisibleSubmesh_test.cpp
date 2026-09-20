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
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/Resources/MaterialBinder.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Resources/TransformUploader.h>
#include <Oxygen/Vortex/ScenePrep/Extractors.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemProto.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/ScenePrepTestFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderTest.h>
#include <Oxygen/Vortex/Test/ScenePrep/ScenePrepHelpers.h>

namespace oxygen::vortex::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::vortex::upload::internal

using oxygen::View;

using oxygen::observer_ptr;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::vortex::sceneprep::EmitPerVisibleSubmesh;
using oxygen::vortex::sceneprep::MeshResolver;
using oxygen::vortex::sceneprep::RenderItemProto;
using oxygen::vortex::sceneprep::ScenePrepContext;
using oxygen::vortex::sceneprep::ScenePrepState;
using oxygen::vortex::sceneprep::SubMeshVisibilityFilter;
using oxygen::vortex::upload::StagingProvider;
using oxygen::vortex::upload::internal::UploaderTagFactory;

using oxygen::vortex::sceneprep::testing::MakeGeometryWithLods;
using oxygen::vortex::sceneprep::testing::ScenePrepTestFixture;

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
    gfx_ = std::make_shared<oxygen::vortex::testing::FakeGraphics>();
    gfx_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    uploader_ = std::make_unique<oxygen::vortex::upload::UploadCoordinator>(
      observer_ptr {
        gfx_.get(),
      });
    staging_provider_ = uploader_->CreateRingBufferStaging(
      oxygen::frame::SlotCount {
        1,
      },
      4, 0.5F);

    // Create resource managers and give ownership to ScenePrepState so
    // Extractors can rely on a non-null material binder during tests.
    geometry_loader_
      = std::make_unique<oxygen::vortex::testing::FakeAssetLoader>();
    auto geom_uploader
      = std::make_unique<oxygen::vortex::resources::GeometryUploader>(
        observer_ptr {
          gfx_.get(),
        },
        observer_ptr {
          uploader_.get(),
        },
        observer_ptr {
          staging_provider_.get(),
        },
        observer_ptr {
          geometry_loader_.get(),
        });
    // We need an InlineTransfersCoordinator instance for the TransformUploader
    // API; the uploader expects an observer_ptr to the inline transfers
    // coordinator.
    inline_transfers_
      = std::make_unique<oxygen::vortex::upload::InlineTransfersCoordinator>(
        observer_ptr {
          gfx_.get(),
        });

    auto transform_uploader
      = std::make_unique<oxygen::vortex::resources::TransformUploader>(
        observer_ptr {
          gfx_.get(),
        },
        observer_ptr {
          staging_provider_.get(),
        },
        observer_ptr {
          inline_transfers_.get(),
        });
    texture_loader_
      = std::make_unique<oxygen::vortex::testing::FakeAssetLoader>();
    texture_binder_
      = std::make_unique<oxygen::vortex::resources::TextureBinder>(
        observer_ptr {
          gfx_.get(),
        },
        observer_ptr {
          staging_provider_.get(),
        },
        observer_ptr {
          uploader_.get(),
        },
        observer_ptr {
          texture_loader_.get(),
        });
    auto material_binder
      = std::make_unique<oxygen::vortex::resources::MaterialBinder>(
        observer_ptr {
          gfx_.get(),
        },
        observer_ptr {
          uploader_.get(),
        },
        observer_ptr {
          staging_provider_.get(),
        },
        observer_ptr {
          texture_binder_.get(),
        },
        observer_ptr {
          texture_loader_.get(),
        });

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
  std::shared_ptr<oxygen::vortex::testing::FakeGraphics> gfx_;
  std::unique_ptr<oxygen::vortex::upload::UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<oxygen::vortex::upload::InlineTransfersCoordinator>
    inline_transfers_;
  std::unique_ptr<oxygen::vortex::resources::TextureBinder> texture_binder_;
  std::unique_ptr<oxygen::vortex::testing::FakeAssetLoader> texture_loader_;
  std::unique_ptr<oxygen::vortex::testing::FakeAssetLoader> geometry_loader_;
};
// Death: dropped item
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, DroppedItem_Death)
{
  EmplaceContextWithView();
  Proto().MarkDropped();
  NOLINT_EXPECT_DEATH(
    EmitPerVisibleSubmesh(Context(), State(), Proto()), "IsDropped");
}

// Death: no resolved mesh
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, NoResolvedMesh_Death)
{
  // Reach the resolved-mesh guard with a valid context and geometry.
  EmplaceContextWithView();
  SetGeometry(MakeGeometryWithLods(1,
    {
      -1,
      -1,
      -1,
    },
    {
      1,
      1,
      1,
    }));
  SeedVisibilityAndTransform();
  ASSERT_NE(Proto().Geometry(), nullptr);
  ASSERT_EQ(Proto().ResolvedMesh(), nullptr);
  NOLINT_EXPECT_DEATH(
    EmitPerVisibleSubmesh(Context(), State(), Proto()), "ResolvedMesh");
}

// Empty visible list -> emits nothing
NOLINT_TEST_F(EmitPerVisibleSubmeshTest, EmptyVisibleList_NoEmission)
{
  // Arrange: resolve mesh but clear visible list
  const auto geom = MakeGeometryWithLods(2,
    {
      -1,
      -1,
      -1,
    },
    {
      1,
      1,
      1,
    });
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

NOLINT_TEST_F(EmitPerVisibleSubmeshTest,
  ReverseWindingUsesInheritedWorldTransformAndCancelsTwoReflections)
{
  const auto geometry = MakeGeometryWithLods(1,
    {
      -1,
      -1,
      -1,
    },
    {
      1,
      1,
      1,
    });
  SetGeometry(geometry);
  auto child = scene_->CreateChildNode(Node(), "MirroredChild");
  if (!child.has_value()) {
    FAIL() << "Expected child to have a value";
  }
  child->GetRenderable().SetGeometry(geometry);
  EmplaceContextWithView();

  struct TransformCase {
    glm::vec3 parent_scale;
    glm::vec3 child_scale;
    bool reverse_winding;
  };
  const auto cases = std::vector<TransformCase> {
    { .parent_scale = { -2.0F, 1.0F, 1.0F, },
      .child_scale = { 1.0F, 1.0F, 1.0F, },
      .reverse_winding = true, },
    { .parent_scale = { -2.0F, 1.0F, 1.0F, },
      .child_scale = { -1.0F, 1.0F, 1.0F, },
      .reverse_winding = false, },
    { .parent_scale = { 2.0F, 1.0F, 1.0F, },
      .child_scale = { -1.0F, 1.0F, 1.0F, },
      .reverse_winding = true, },
    { .parent_scale = { -2.0F, -1.0F, 1.0F, },
      .child_scale = { 1.0F, 1.0F, 1.0F, },
      .reverse_winding = false, },
  };
  for (const auto& test_case : cases) {
    Node().GetTransform().SetLocalTransform(glm::vec3(0.0F),
      glm::quat(1.0F, 0.0F, 0.0F, 0.0F), test_case.parent_scale);
    child->GetTransform().SetLocalTransform(glm::vec3(0.0F),
      glm::quat(1.0F, 0.0F, 0.0F, 0.0F), test_case.child_scale);
    UpdateScene();
    const auto child_impl = child->GetImpl();
    if (!child_impl.has_value()) {
      FAIL() << "Expected child_impl to have a value";
    }
    auto item = RenderItemProto(child_impl->get());
    oxygen::vortex::sceneprep::ExtractionPreFilter(Context(), State(), item);
    ASSERT_FALSE(item.IsDropped());
    item.ResolveMesh(geometry->MeshAt(0U), 0U);
    item.SetVisibleSubmeshes({
      0U,
    });
    const auto before = State().CollectedCount();
    EmitPerVisibleSubmesh(Context(), State(), item);
    ASSERT_EQ(State().CollectedCount(), before + 1U);
    EXPECT_EQ(State().CollectedItems().back().reverse_winding,
      test_case.reverse_winding);
  }
}

} // namespace
