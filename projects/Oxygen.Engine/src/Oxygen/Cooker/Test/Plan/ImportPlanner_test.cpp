//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/ImportPlanner.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::co::Co;
using oxygen::content::import::DependencyToken;
using oxygen::content::import::ImportPlanner;
using oxygen::content::import::PipelineProgress;
using oxygen::content::import::PlanItemId;
using oxygen::content::import::PlanItemKind;
using oxygen::content::import::PlanStep;

namespace {

template <oxygen::TypeId kTypeId> struct MockPipeline {
  using WorkItem = int;
  using WorkResult = int;

  static constexpr PlanItemKind kItemKind = PlanItemKind::kTextureResource;

  static auto ClassTypeId() noexcept -> oxygen::TypeId { return kTypeId; }

  auto Start(oxygen::co::Nursery& nursery) -> void { (void)nursery; }

  auto Submit(WorkItem item) -> Co<>
  {
    (void)item;
    co_return;
  }

  auto Collect() -> Co<WorkResult> { co_return WorkResult {}; }

  [[nodiscard]] auto HasPending() const -> bool { return false; }

  [[nodiscard]] auto PendingCount() const -> size_t { return 0U; }

  [[nodiscard]] auto GetProgress() const -> PipelineProgress { return {}; }

  [[nodiscard]] auto OutputQueueSize() const -> size_t { return 0U; }

  [[nodiscard]] auto OutputQueueCapacity() const -> size_t { return 0U; }
};

constexpr oxygen::TypeId kMockTexturePipelineTypeId = 0x1101U;
constexpr oxygen::TypeId kMockBufferPipelineTypeId = 0x1102U;
constexpr oxygen::TypeId kMockAudioPipelineTypeId = 0x1103U;
constexpr oxygen::TypeId kMockMaterialPipelineTypeId = 0x1104U;
constexpr oxygen::TypeId kMockGeometryPipelineTypeId = 0x1105U;
constexpr oxygen::TypeId kMockScenePipelineTypeId = 0x1106U;

using MockTexturePipeline = MockPipeline<kMockTexturePipelineTypeId>;
using MockBufferPipeline = MockPipeline<kMockBufferPipelineTypeId>;
using MockAudioPipeline = MockPipeline<kMockAudioPipelineTypeId>;
using MockMaterialPipeline = MockPipeline<kMockMaterialPipelineTypeId>;
using MockGeometryPipeline = MockPipeline<kMockGeometryPipelineTypeId>;
using MockScenePipeline = MockPipeline<kMockScenePipelineTypeId>;

class ImportPlannerPlanTest : public ::testing::Test {
protected:
  [[nodiscard]] auto Planner() noexcept -> ImportPlanner& { return planner_; }
  [[nodiscard]] auto Planner() const noexcept -> const ImportPlanner&
  {
    return planner_;
  }

  auto RegisterAllPipelines() -> void
  {
    Planner().RegisterPipeline<MockTexturePipeline>(
      PlanItemKind::kTextureResource);
    Planner().RegisterPipeline<MockBufferPipeline>(
      PlanItemKind::kBufferResource);
    Planner().RegisterPipeline<MockAudioPipeline>(PlanItemKind::kAudioResource);
    Planner().RegisterPipeline<MockMaterialPipeline>(
      PlanItemKind::kMaterialAsset);
    Planner().RegisterPipeline<MockGeometryPipeline>(
      PlanItemKind::kGeometryAsset);
    Planner().RegisterPipeline<MockScenePipeline>(PlanItemKind::kSceneAsset);
  }

  [[nodiscard]] auto FindStep(const std::vector<PlanStep>& plan, PlanItemId id)
    -> const PlanStep*
  {
    const auto it = std::ranges::find_if(
      plan, [&](const PlanStep& step) -> bool { return step.item_id == id; });

    if (it == plan.end()) {
      return nullptr;
    }

    return &(*it);
  }

private:
  ImportPlanner planner_;
};

//! Validate stable topological order follows registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanStableOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto material = Planner().AddMaterialAsset("material", {});
  const auto geometry = Planner().AddGeometryAsset("geometry", {});
  const auto scene = Planner().AddSceneAsset("scene", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(geometry, material);
  Planner().AddDependency(geometry, buffer);
  Planner().AddDependency(scene, geometry);

  // Act
  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanTieBreaksByOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto audio = Planner().AddAudioResource("audio", {});

  // Act
  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerAddDependencyDeduplicates)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(material, texture);

  // Act
  const auto plan = Planner().MakePlan();

  // Assert
  const auto* step = FindStep(plan, material);
  ASSERT_NE(step, nullptr);
  EXPECT_EQ(step->prerequisites.size(), 1U);
  EXPECT_EQ(Planner().Tracker(material).required.size(), 1U);
}

//! Validate pipeline resolution returns registered type IDs.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerPipelineTypeForResolves)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto scene = Planner().AddSceneAsset("scene", {});

  // Act
  const auto plan = Planner().MakePlan();
  (void)plan;

  // Assert
  EXPECT_EQ(
    Planner().PipelineTypeFor(texture), MockTexturePipeline::ClassTypeId());
  EXPECT_EQ(Planner().PipelineTypeFor(scene), MockScenePipeline::ClassTypeId());
}

//! Validate readiness transitions once all producers are marked ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerReadinessTrackerTransitions)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(material, buffer);

  const auto plan = Planner().MakePlan();
  (void)plan;

  auto& tracker = Planner().Tracker(material);

  // Act
  const auto first_result = tracker.MarkReady({ texture });
  const auto second_result = tracker.MarkReady({ buffer });
  const auto duplicate_result = tracker.MarkReady({ buffer });

  // Assert
  EXPECT_FALSE(first_result);
  EXPECT_TRUE(second_result);
  EXPECT_FALSE(duplicate_result);
  EXPECT_TRUE(tracker.IsReady());
  EXPECT_TRUE(Planner().ReadyEvent(material).ready);
  EXPECT_TRUE(Planner().ReadyEvent(material).event.Triggered());
}

//! Ensure items with no dependencies are immediately ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerReadinessTrackerEmptyReady)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  // Act
  const auto plan = Planner().MakePlan();
  (void)plan;

  // Assert
  EXPECT_TRUE(Planner().Tracker(texture).IsReady());
  EXPECT_TRUE(Planner().ReadyEvent(texture).ready);
  EXPECT_TRUE(Planner().ReadyEvent(texture).event.Triggered());
}

//! Validate empty planner builds an empty plan.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanEmptyPlan)
{
  // Arrange
  RegisterAllPipelines();

  // Act
  const auto plan = Planner().MakePlan();

  // Assert
  EXPECT_TRUE(plan.empty());
}

//! Ensure MarkReady ignores unknown producer tokens.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerReadinessTrackerUnknownToken)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);

  const auto plan = Planner().MakePlan();
  (void)plan;

  auto& tracker = Planner().Tracker(material);
  const DependencyToken unknown { PlanItemId { 999U } };

  // Act
  const auto result = tracker.MarkReady(unknown);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_FALSE(tracker.IsReady());
  EXPECT_FALSE(Planner().ReadyEvent(material).ready);
}

//! Validate self-dependency is detected as a cycle.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanSelfCycleDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  Planner().AddDependency(texture, texture);

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

//! Validate disjoint subgraphs preserve registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanDisjointOrder)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto geometry = Planner().AddGeometryAsset("geometry", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(geometry, buffer);

  // Act
  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanOrderDiffersFromId)
{
  // Arrange
  RegisterAllPipelines();

  const auto early = Planner().AddTextureResource("early", {});
  const auto middle = Planner().AddBufferResource("middle", {});
  const auto late = Planner().AddMaterialAsset("late", {});

  Planner().AddDependency(early, late);

  // Act
  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanComplexScene)
{
  // Arrange
  RegisterAllPipelines();

  const auto scene = Planner().AddSceneAsset("scene", {});

  const auto lod0 = Planner().AddGeometryAsset("geom_lod0", {});
  Planner().AddDependency(scene, lod0);

  const auto lod1 = Planner().AddGeometryAsset("geom_lod1", {});
  Planner().AddDependency(scene, lod1);

  const auto material_a = Planner().AddMaterialAsset("material_a", {});
  Planner().AddDependency(lod0, material_a);
  Planner().AddDependency(lod1, material_a);

  const auto material_b = Planner().AddMaterialAsset("material_b", {});
  Planner().AddDependency(lod0, material_b);
  Planner().AddDependency(lod1, material_b);

  const auto albedo = Planner().AddTextureResource("albedo", {});
  Planner().AddDependency(material_a, albedo);
  Planner().AddDependency(material_b, albedo);

  const auto normal = Planner().AddTextureResource("normal", {});
  Planner().AddDependency(material_a, normal);

  const auto roughness = Planner().AddTextureResource("roughness", {});
  Planner().AddDependency(material_a, roughness);

  const auto metalness = Planner().AddTextureResource("metalness", {});
  Planner().AddDependency(material_b, metalness);

  const auto vertex_buffer = Planner().AddBufferResource("vb", {});
  Planner().AddDependency(lod0, vertex_buffer);
  Planner().AddDependency(lod1, vertex_buffer);

  const auto index_buffer = Planner().AddBufferResource("ib", {});
  Planner().AddDependency(lod0, index_buffer);
  Planner().AddDependency(lod1, index_buffer);

  const auto data_buffer = Planner().AddBufferResource("custom_data", {});

  // Act
  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerPipelineTypeForPreSeal)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  // Act
  const auto pipeline_type = Planner().PipelineTypeFor(texture);

  // Assert
  EXPECT_EQ(pipeline_type, MockTexturePipeline::ClassTypeId());
}

//! Validate missing pipeline registration is a blocking error.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanMissingPipelineDies)
{
  // Arrange
  (void)Planner().AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "Missing pipeline registration");
}

//! Validate cycle detection triggers a blocking error.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanCycleDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(texture, material);

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

//! Validate upstream planning rejects cyclic asset authoring before runtime.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanCrossAssetCycleDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto scene = Planner().AddSceneAsset("scene", {});
  const auto geometry = Planner().AddGeometryAsset("geometry", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(scene, geometry);
  Planner().AddDependency(geometry, material);
  Planner().AddDependency(material, scene);

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

//! Verify mutations are blocked after the planner is sealed.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerAddAfterSealDies)
{
  // Arrange
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});
  (void)Planner().MakePlan();

  // Act + Assert
  NOLINT_EXPECT_DEATH(
    Planner().AddBufferResource("buffer", {}), "sealed and cannot be modified");
}

//! Validate MakePlan cannot be called twice.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerMakePlanTwiceDies)
{
  // Arrange
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});
  (void)Planner().MakePlan();

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "sealed and cannot be modified");
}

//! Validate invalid PlanItemId access is rejected.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerInvalidItemIdDies)
{
  // Arrange
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(
    Planner().Item(PlanItemId { 42U }), "PlanItemId out of range");
}

//! Validate invalid dependency references are rejected.
NOLINT_TEST_F(ImportPlannerPlanTest, ImportPlannerAddDependencyInvalidDies)
{
  // Arrange
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  // Act + Assert
  NOLINT_EXPECT_DEATH(Planner().AddDependency(texture, PlanItemId { 99U }),
    "PlanItemId out of range");
}

} // namespace
