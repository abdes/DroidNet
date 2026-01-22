//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/Internal/ImportPlanner.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using oxygen::co::Co;

namespace {

template <oxygen::TypeId kTypeId> struct MockPipeline {
  using WorkItem = int;
  using WorkResult = int;

  static constexpr PlanItemKind kItemKind = PlanItemKind::kTextureResource;

  static auto ClassTypeId() noexcept -> oxygen::TypeId { return kTypeId; }

  auto Start(oxygen::co::Nursery&) -> void { }

  auto Submit(WorkItem) -> Co<> { co_return; }

  auto Collect() -> Co<WorkResult> { co_return WorkResult {}; }

  [[nodiscard]] auto HasPending() const -> bool { return false; }

  [[nodiscard]] auto PendingCount() const -> size_t { return 0U; }

  [[nodiscard]] auto GetProgress() const -> PipelineProgress { return {}; }

  [[nodiscard]] auto OutputQueueSize() const -> size_t { return 0U; }

  [[nodiscard]] auto OutputQueueCapacity() const -> size_t { return 0U; }
};

using MockTexturePipeline = MockPipeline<0x1101>;
using MockBufferPipeline = MockPipeline<0x1102>;
using MockAudioPipeline = MockPipeline<0x1103>;
using MockMaterialPipeline = MockPipeline<0x1104>;
using MockGeometryPipeline = MockPipeline<0x1105>;
using MockScenePipeline = MockPipeline<0x1106>;

class ImportPlannerPlanTest : public ::testing::Test {
protected:
  ImportPlanner planner_;

  auto RegisterAllPipelines() -> void
  {
    planner_.RegisterPipeline<MockTexturePipeline>(
      PlanItemKind::kTextureResource);
    planner_.RegisterPipeline<MockBufferPipeline>(
      PlanItemKind::kBufferResource);
    planner_.RegisterPipeline<MockAudioPipeline>(PlanItemKind::kAudioResource);
    planner_.RegisterPipeline<MockMaterialPipeline>(
      PlanItemKind::kMaterialAsset);
    planner_.RegisterPipeline<MockGeometryPipeline>(
      PlanItemKind::kGeometryAsset);
    planner_.RegisterPipeline<MockScenePipeline>(PlanItemKind::kSceneAsset);
  }

  [[nodiscard]] auto FindStep(const std::vector<PlanStep>& plan, PlanItemId id)
    -> const PlanStep*
  {
    const auto it = std::find_if(plan.begin(), plan.end(),
      [&](const PlanStep& step) { return step.item_id == id; });

    if (it == plan.end()) {
      return nullptr;
    }

    return &(*it);
  }
};

//! Validate stable topological order follows registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_StableOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto buffer = planner_.AddBufferResource("buffer", {});
  const auto material = planner_.AddMaterialAsset("material", {});
  const auto geometry = planner_.AddGeometryAsset("geometry", {});
  const auto scene = planner_.AddSceneAsset("scene", {});

  planner_.AddDependency(material, texture);
  planner_.AddDependency(geometry, material);
  planner_.AddDependency(geometry, buffer);
  planner_.AddDependency(scene, geometry);

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected
    = { texture, buffer, material, geometry, scene };
  EXPECT_EQ(order, expected);
}

//! Verify tie-breaking uses registration order for independent items.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_TieBreaksByOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto buffer = planner_.AddBufferResource("buffer", {});
  const auto audio = planner_.AddAudioResource("audio", {});

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected = { texture, buffer, audio };
  EXPECT_EQ(order, expected);
}

//! Ensure dependencies are deduplicated by producer per consumer.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_AddDependency_Deduplicates)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto material = planner_.AddMaterialAsset("material", {});

  planner_.AddDependency(material, texture);
  planner_.AddDependency(material, texture);

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  const auto* step = FindStep(plan, material);
  ASSERT_NE(step, nullptr);
  EXPECT_EQ(step->prerequisites.size(), 1U);
  EXPECT_EQ(planner_.Tracker(material).required.size(), 1U);
}

//! Validate pipeline resolution returns registered type IDs.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_PipelineTypeFor_Resolves)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto scene = planner_.AddSceneAsset("scene", {});

  // Act
  const auto plan = planner_.MakePlan();
  (void)plan;

  // Assert
  EXPECT_EQ(
    planner_.PipelineTypeFor(texture), MockTexturePipeline::ClassTypeId());
  EXPECT_EQ(planner_.PipelineTypeFor(scene), MockScenePipeline::ClassTypeId());
}

//! Validate readiness transitions once all producers are marked ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_ReadinessTracker_Transitions)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto buffer = planner_.AddBufferResource("buffer", {});
  const auto material = planner_.AddMaterialAsset("material", {});

  planner_.AddDependency(material, texture);
  planner_.AddDependency(material, buffer);

  const auto plan = planner_.MakePlan();
  (void)plan;

  auto& tracker = planner_.Tracker(material);

  // Act
  const auto first_result = tracker.MarkReady({ texture });
  const auto second_result = tracker.MarkReady({ buffer });
  const auto duplicate_result = tracker.MarkReady({ buffer });

  // Assert
  EXPECT_FALSE(first_result);
  EXPECT_TRUE(second_result);
  EXPECT_FALSE(duplicate_result);
  EXPECT_TRUE(tracker.IsReady());
  EXPECT_TRUE(planner_.ReadyEvent(material).ready);
  EXPECT_TRUE(planner_.ReadyEvent(material).event.Triggered());
}

//! Ensure items with no dependencies are immediately ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_ReadinessTracker_EmptyReady)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});

  // Act
  const auto plan = planner_.MakePlan();
  (void)plan;

  // Assert
  EXPECT_TRUE(planner_.Tracker(texture).IsReady());
  EXPECT_TRUE(planner_.ReadyEvent(texture).ready);
  EXPECT_TRUE(planner_.ReadyEvent(texture).event.Triggered());
}

//! Validate empty planner builds an empty plan.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_EmptyPlan)
{
  // Arrange
  RegisterAllPipelines();

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  EXPECT_TRUE(plan.empty());
}

//! Ensure MarkReady ignores unknown producer tokens.
NOLINT_TEST_F(
  ImportPlannerPlanTest, ImportPlanner_ReadinessTracker_UnknownToken)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto material = planner_.AddMaterialAsset("material", {});

  planner_.AddDependency(material, texture);

  const auto plan = planner_.MakePlan();
  (void)plan;

  auto& tracker = planner_.Tracker(material);
  const DependencyToken unknown { PlanItemId { 999U } };

  // Act
  const auto result = tracker.MarkReady(unknown);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_FALSE(tracker.IsReady());
  EXPECT_FALSE(planner_.ReadyEvent(material).ready);
}

//! Validate self-dependency is detected as a cycle.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_SelfCycleDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  planner_.AddDependency(texture, texture);

  // Act + Assert
  NOLINT_EXPECT_DEATH(planner_.MakePlan(), "cycle detected");
}

//! Validate disjoint subgraphs preserve registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_DisjointOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto material = planner_.AddMaterialAsset("material", {});
  const auto buffer = planner_.AddBufferResource("buffer", {});
  const auto geometry = planner_.AddGeometryAsset("geometry", {});

  planner_.AddDependency(material, texture);
  planner_.AddDependency(geometry, buffer);

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected
    = { texture, buffer, material, geometry };
  EXPECT_EQ(order, expected);
}

//! Validate plan order can differ from registration IDs.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_OrderDiffersFromId)
{
  // Arrange
  RegisterAllPipelines();

  const auto early = planner_.AddTextureResource("early", {});
  const auto middle = planner_.AddBufferResource("middle", {});
  const auto late = planner_.AddMaterialAsset("late", {});

  planner_.AddDependency(early, late);

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected = { middle, late, early };
  EXPECT_EQ(order, expected);
  EXPECT_NE(order.front(), early);
}

//! Validate complex scene dependencies with LODs and buffers.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_ComplexScene)
{
  // Arrange
  RegisterAllPipelines();

  const auto scene = planner_.AddSceneAsset("scene", {});

  const auto lod0 = planner_.AddGeometryAsset("geom_lod0", {});
  planner_.AddDependency(scene, lod0);

  const auto lod1 = planner_.AddGeometryAsset("geom_lod1", {});
  planner_.AddDependency(scene, lod1);

  const auto material_a = planner_.AddMaterialAsset("material_a", {});
  planner_.AddDependency(lod0, material_a);
  planner_.AddDependency(lod1, material_a);

  const auto material_b = planner_.AddMaterialAsset("material_b", {});
  planner_.AddDependency(lod0, material_b);
  planner_.AddDependency(lod1, material_b);

  const auto albedo = planner_.AddTextureResource("albedo", {});
  planner_.AddDependency(material_a, albedo);
  planner_.AddDependency(material_b, albedo);

  const auto normal = planner_.AddTextureResource("normal", {});
  planner_.AddDependency(material_a, normal);

  const auto roughness = planner_.AddTextureResource("roughness", {});
  planner_.AddDependency(material_a, roughness);

  const auto metalness = planner_.AddTextureResource("metalness", {});
  planner_.AddDependency(material_b, metalness);

  const auto vertex_buffer = planner_.AddBufferResource("vb", {});
  planner_.AddDependency(lod0, vertex_buffer);
  planner_.AddDependency(lod1, vertex_buffer);

  const auto index_buffer = planner_.AddBufferResource("ib", {});
  planner_.AddDependency(lod0, index_buffer);
  planner_.AddDependency(lod1, index_buffer);

  const auto data_buffer = planner_.AddBufferResource("custom_data", {});

  // Act
  const auto plan = planner_.MakePlan();

  // Assert
  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected
    = { albedo, normal, roughness, metalness, vertex_buffer, index_buffer,
        data_buffer, material_a, material_b, lod0, lod1, scene };
  EXPECT_EQ(order, expected);
}

//! Validate pipeline resolution works before MakePlan sealing.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_PipelineTypeFor_PreSeal)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});

  // Act
  const auto pipeline_type = planner_.PipelineTypeFor(texture);

  // Assert
  EXPECT_EQ(pipeline_type, MockTexturePipeline::ClassTypeId());
}

//! Validate missing pipeline registration is a blocking error.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_MissingPipelineDies)
{
  // Arrange
  (void)planner_.AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(planner_.MakePlan(), "Missing pipeline registration");
}

//! Validate cycle detection triggers a blocking error.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_CycleDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});
  const auto material = planner_.AddMaterialAsset("material", {});

  planner_.AddDependency(material, texture);
  planner_.AddDependency(texture, material);

  // Act + Assert
  NOLINT_EXPECT_DEATH(planner_.MakePlan(), "cycle detected");
}

//! Verify mutations are blocked after the planner is sealed.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_AddAfterSeal_Dies)
{
  // Arrange
  RegisterAllPipelines();

  (void)planner_.AddTextureResource("texture", {});
  (void)planner_.MakePlan();

  // Act + Assert
  NOLINT_EXPECT_DEATH(
    planner_.AddBufferResource("buffer", {}), "sealed and cannot be modified");
}

//! Validate MakePlan cannot be called twice.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_MakePlan_TwiceDies)
{
  // Arrange
  RegisterAllPipelines();

  (void)planner_.AddTextureResource("texture", {});
  (void)planner_.MakePlan();

  // Act + Assert
  NOLINT_EXPECT_DEATH(planner_.MakePlan(), "sealed and cannot be modified");
}

//! Validate invalid PlanItemId access is rejected.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_InvalidItemIdDies)
{
  // Arrange
  RegisterAllPipelines();

  (void)planner_.AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(
    planner_.Item(PlanItemId { 42U }), "PlanItemId out of range");
}

//! Validate invalid dependency references are rejected.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlanner_AddDependency_InvalidDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = planner_.AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(planner_.AddDependency(texture, PlanItemId { 99U }),
    "PlanItemId out of range");
}

} // namespace
