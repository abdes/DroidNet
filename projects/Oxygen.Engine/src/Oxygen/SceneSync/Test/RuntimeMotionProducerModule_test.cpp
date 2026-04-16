//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::scenesync::test {

namespace {

  auto MakeSolidColorMaterial(const char* name, const float red)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    namespace pak = oxygen::data::pak;

    pak::render::MaterialAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    constexpr std::size_t kMaxName = sizeof(desc.header.name) - 1U;
    const auto name_length = (std::min)(kMaxName, std::strlen(name));
    std::memcpy(desc.header.name, name, name_length);
    desc.header.name[name_length] = '\0';
    desc.header.version = pak::render::kMaterialAssetVersion;
    desc.material_domain
      = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
    desc.base_color[0] = red;
    desc.base_color[1] = 0.4F;
    desc.base_color[2] = 0.5F;
    desc.base_color[3] = 1.0F;

    return std::make_shared<const oxygen::data::MaterialAsset>(
      oxygen::data::AssetKey::FromVirtualPath(
        "/Tests/SceneSync/" + std::string(name) + ".omat"),
      desc, std::vector<oxygen::data::ShaderReference> {});
  }

  auto BuildSingleSubmeshGeometry(const char* geometry_name,
    std::shared_ptr<const data::MaterialAsset> material)
    -> std::shared_ptr<data::GeometryAsset>
  {
    namespace d = oxygen::data;
    namespace pak = oxygen::data::pak;

    const auto cube = d::MakeCubeMeshAsset();
    if (!cube.has_value()) {
      return {};
    }

    auto mesh
      = d::MeshBuilder(0, geometry_name)
          .WithVertices(cube->first)
          .WithIndices(cube->second)
          .BeginSubMesh("full", std::move(material))
          .WithMeshView(pak::geometry::MeshViewDesc {
            .first_index = 0U,
            .index_count = static_cast<uint32_t>(cube->second.size()),
            .first_vertex = 0U,
            .vertex_count = static_cast<uint32_t>(cube->first.size()),
          })
          .EndSubMesh()
          .Build();

    pak::geometry::GeometryAssetDesc desc {};
    desc.lod_count = 1U;
    const auto bbox_min = mesh->BoundingBoxMin();
    const auto bbox_max = mesh->BoundingBoxMax();
    desc.bounding_box_min[0] = bbox_min.x;
    desc.bounding_box_min[1] = bbox_min.y;
    desc.bounding_box_min[2] = bbox_min.z;
    desc.bounding_box_max[0] = bbox_max.x;
    desc.bounding_box_max[1] = bbox_max.y;
    desc.bounding_box_max[2] = bbox_max.z;

    return std::make_shared<d::GeometryAsset>(
      d::AssetKey::FromVirtualPath(
        "/Tests/SceneSync/" + std::string(geometry_name) + ".ogeo"),
      desc, std::vector<std::shared_ptr<d::Mesh>> { std::move(mesh) });
  }

  auto PublishSnapshot(RuntimeMotionProducerModule& module,
    engine::FrameContext& frame, const frame::SequenceNumber frame_sequence)
    -> void
  {
    using Tag = oxygen::engine::internal::EngineTagFactory;

    frame.SetCurrentPhase(core::PhaseId::kPublishViews, Tag::Get());
    frame.SetFrameSequenceNumber(frame_sequence, Tag::Get());

    co::testing::TestEventLoop loop {};
    co::Run(loop, [&]() -> co::Co<> {
      co_await module.OnPublishViews(observer_ptr<engine::FrameContext> { &frame });
      co_return;
    });
  }

} // namespace

NOLINT_TEST(RuntimeMotionProducerModuleTest, UsesReservedPriorityAndPhases)
{
  RuntimeMotionProducerModule module {};

  EXPECT_EQ(module.GetPriority().get(),
    engine::kRuntimeMotionProducerModulePriority.get());

  const auto phases = module.GetSupportedPhases();
  EXPECT_NE(phases & MakePhaseMask(core::PhaseId::kSceneMutation), 0U);
  EXPECT_NE(phases & MakePhaseMask(core::PhaseId::kPublishViews), 0U);
}

NOLINT_TEST(RuntimeMotionProducerModuleTest,
  PublishViewsFreezesMaterialMotionStateForRenderableScene)
{
  auto scene = std::make_shared<scene::Scene>("RuntimeMotionScene", 16U);
  auto material = MakeSolidColorMaterial("material_a", 0.2F);
  auto geometry = BuildSingleSubmeshGeometry("cube_a", material);
  ASSERT_NE(geometry, nullptr);

  auto node = scene->CreateNode("Renderable");
  node.GetRenderable().SetGeometry(std::move(geometry));
  scene->Update();

  engine::FrameContext frame {};
  frame.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RuntimeMotionProducerModule module {};
  PublishSnapshot(module, frame, frame::SequenceNumber { 7U });

  const auto* snapshot
    = module.GetPublishedSnapshot(observer_ptr<scene::Scene> { scene.get() });
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(snapshot->frame_sequence, frame::SequenceNumber { 7U });
  EXPECT_TRUE(snapshot->morph_deformation_states.empty());
  ASSERT_EQ(snapshot->material_motion_states.size(), 1U);

  const auto& state = snapshot->material_motion_states.front();
  EXPECT_EQ(state.key.node_handle, node.GetHandle());
  EXPECT_EQ(state.key.lod_index, 0U);
  EXPECT_EQ(state.key.submesh_index, 0U);
  EXPECT_EQ(state.resolved_material_asset_key, material->GetAssetKey());
  EXPECT_FALSE(state.has_runtime_wpo_input);
  EXPECT_FALSE(state.has_runtime_motion_vector_input);
  EXPECT_FALSE(state.capabilities.uses_world_position_offset);
  EXPECT_FALSE(state.capabilities.uses_motion_vector_world_offset);
  EXPECT_FALSE(state.capabilities.uses_temporal_responsiveness);
  EXPECT_FALSE(state.capabilities.has_pixel_animation);
  EXPECT_EQ(state.wpo_parameter_block0,
    (std::array<float, 4U> { 0.0F, 0.0F, 0.0F, 0.0F }));
  EXPECT_EQ(state.motion_vector_parameter_block0,
    (std::array<float, 4U> { 0.0F, 0.0F, 0.0F, 0.0F }));
  EXPECT_NE(state.contract_hash, 0U);

  const auto* found = snapshot->FindMaterialMotionState(state.key);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->contract_hash, state.contract_hash);
}

NOLINT_TEST(RuntimeMotionProducerModuleTest,
  MaterialMotionKeyStaysStableAcrossMaterialOverrideChanges)
{
  auto scene = std::make_shared<scene::Scene>("RuntimeMotionOverrideScene", 16U);
  auto initial_material = MakeSolidColorMaterial("material_initial", 0.15F);
  auto override_material = MakeSolidColorMaterial("material_override", 0.85F);
  auto geometry = BuildSingleSubmeshGeometry("cube_override", initial_material);
  ASSERT_NE(geometry, nullptr);

  auto node = scene->CreateNode("RenderableOverride");
  node.GetRenderable().SetGeometry(geometry);
  scene->Update();

  engine::FrameContext frame {};
  frame.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RuntimeMotionProducerModule module {};
  PublishSnapshot(module, frame, frame::SequenceNumber { 9U });
  const auto* before
    = module.GetPublishedSnapshot(observer_ptr<scene::Scene> { scene.get() });
  ASSERT_NE(before, nullptr);
  ASSERT_EQ(before->material_motion_states.size(), 1U);
  const auto before_key = before->material_motion_states.front().key;
  const auto before_hash = before->material_motion_states.front().contract_hash;

  node.GetRenderable().SetMaterialOverride(0U, 0U, override_material);
  scene->Update();

  PublishSnapshot(module, frame, frame::SequenceNumber { 10U });
  const auto* after
    = module.GetPublishedSnapshot(observer_ptr<scene::Scene> { scene.get() });
  ASSERT_NE(after, nullptr);
  ASSERT_EQ(after->material_motion_states.size(), 1U);

  const auto& state = after->material_motion_states.front();
  EXPECT_EQ(state.key, before_key);
  EXPECT_EQ(state.resolved_material_asset_key, override_material->GetAssetKey());
  EXPECT_NE(state.contract_hash, before_hash);
}

NOLINT_TEST(RuntimeMotionProducerModuleTest,
  PublishViewsOverlaysRuntimeMaterialMotionInputsOntoFrozenSnapshot)
{
  auto scene = std::make_shared<scene::Scene>("RuntimeMotionInputScene", 16U);
  auto material = MakeSolidColorMaterial("material_runtime_input", 0.3F);
  auto geometry = BuildSingleSubmeshGeometry("cube_runtime_input", material);
  ASSERT_NE(geometry, nullptr);

  auto node = scene->CreateNode("RenderableRuntimeInput");
  node.GetRenderable().SetGeometry(std::move(geometry));
  scene->Update();

  engine::FrameContext frame {};
  frame.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RuntimeMotionProducerModule module {};
  const auto key = RuntimeMaterialMotionKey {
    .node_handle = node.GetHandle(),
    .geometry_asset_key = node.GetRenderable().GetGeometry()->GetAssetKey(),
    .lod_index = 0U,
    .submesh_index = 0U,
  };
  module.UpsertMaterialMotionInput(observer_ptr<const scene::Scene> { scene.get() },
    RuntimeMaterialMotionInputState {
      .key = key,
      .resolved_material_asset_key = material->GetAssetKey(),
      .contract_hash = 0xABCDEFU,
      .has_runtime_wpo_input = true,
      .has_runtime_motion_vector_input = true,
      .capabilities =
        {
          .uses_world_position_offset = true,
          .uses_motion_vector_world_offset = true,
          .uses_temporal_responsiveness = true,
          .has_pixel_animation = true,
        },
      .wpo_parameter_block0 = { 1.0F, 2.0F, 3.0F, 4.0F },
      .motion_vector_parameter_block0 = { 5.0F, 6.0F, 7.0F, 8.0F },
    });

  PublishSnapshot(module, frame, frame::SequenceNumber { 11U });
  const auto* snapshot
    = module.GetPublishedSnapshot(observer_ptr<scene::Scene> { scene.get() });
  ASSERT_NE(snapshot, nullptr);
  ASSERT_EQ(snapshot->material_motion_states.size(), 1U);

  const auto& state = snapshot->material_motion_states.front();
  EXPECT_EQ(state.key, key);
  EXPECT_EQ(state.contract_hash, 0xABCDEFU);
  EXPECT_TRUE(state.has_runtime_wpo_input);
  EXPECT_TRUE(state.has_runtime_motion_vector_input);
  EXPECT_TRUE(state.capabilities.uses_world_position_offset);
  EXPECT_TRUE(state.capabilities.uses_motion_vector_world_offset);
  EXPECT_TRUE(state.capabilities.uses_temporal_responsiveness);
  EXPECT_TRUE(state.capabilities.has_pixel_animation);
  EXPECT_EQ(state.wpo_parameter_block0,
    (std::array<float, 4U> { 1.0F, 2.0F, 3.0F, 4.0F }));
  EXPECT_EQ(state.motion_vector_parameter_block0,
    (std::array<float, 4U> { 5.0F, 6.0F, 7.0F, 8.0F }));
}

} // namespace oxygen::scenesync::test
