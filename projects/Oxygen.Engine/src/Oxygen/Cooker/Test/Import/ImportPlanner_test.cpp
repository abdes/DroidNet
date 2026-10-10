//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportPlanner.cpp

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImportPlanner.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Testing/GTest.h>

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

using ImportPlannerDeathTest = ImportPlannerPlanTest;

//! Validate stable topological order follows registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanStableOrder)
{
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

  const auto plan = Planner().MakePlan();

  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected
    = { texture, buffer, material, geometry, scene };
  EXPECT_EQ(order, expected);
}

NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanTieBreaksByOrder)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto audio = Planner().AddAudioResource("audio", {});

  const auto plan = Planner().MakePlan();

  std::vector<PlanItemId> order;
  order.reserve(plan.size());
  for (const auto& step : plan) {
    order.push_back(step.item_id);
  }

  const std::vector<PlanItemId> expected = { texture, buffer, audio };
  EXPECT_EQ(order, expected);
}

//! Ensure dependencies are deduplicated by producer per consumer.
NOLINT_TEST_F(ImportPlannerPlanTest, AddDependencyDeduplicates)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(material, texture);

  const auto plan = Planner().MakePlan();

  const auto* step = FindStep(plan, material);
  ASSERT_NE(step, nullptr);
  EXPECT_EQ(step->prerequisites.size(), 1U);
  EXPECT_EQ(Planner().Tracker(material).required.size(), 1U);
}

//! Validate pipeline resolution returns registered type IDs.
NOLINT_TEST_F(ImportPlannerPlanTest, PipelineTypeForResolves)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto scene = Planner().AddSceneAsset("scene", {});

  const auto plan = Planner().MakePlan();
  (void)plan;

  EXPECT_EQ(
    Planner().PipelineTypeFor(texture), MockTexturePipeline::ClassTypeId());
  EXPECT_EQ(Planner().PipelineTypeFor(scene), MockScenePipeline::ClassTypeId());
}

//! Validate readiness transitions once all producers are marked ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ReadinessTrackerTransitions)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(material, buffer);

  const auto plan = Planner().MakePlan();
  (void)plan;

  auto& tracker = Planner().Tracker(material);

  const auto first_result = tracker.MarkReady({ texture });
  const auto second_result = tracker.MarkReady({ buffer });
  const auto duplicate_result = tracker.MarkReady({ buffer });

  EXPECT_FALSE(first_result);
  EXPECT_TRUE(second_result);
  EXPECT_FALSE(duplicate_result);
  EXPECT_TRUE(tracker.IsReady());
  EXPECT_TRUE(Planner().ReadyEvent(material).ready);
  EXPECT_TRUE(Planner().ReadyEvent(material).event.Triggered());
}

//! Ensure items with no dependencies are immediately ready.
NOLINT_TEST_F(ImportPlannerPlanTest, ReadinessTrackerEmptyReady)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  const auto plan = Planner().MakePlan();
  (void)plan;

  EXPECT_TRUE(Planner().Tracker(texture).IsReady());
  EXPECT_TRUE(Planner().ReadyEvent(texture).ready);
  EXPECT_TRUE(Planner().ReadyEvent(texture).event.Triggered());
}

//! Validate empty planner builds an empty plan.
NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanEmptyPlan)
{
  RegisterAllPipelines();

  const auto plan = Planner().MakePlan();

  EXPECT_TRUE(plan.empty());
}

//! Ensure MarkReady ignores unknown producer tokens.
NOLINT_TEST_F(ImportPlannerPlanTest, ReadinessTrackerUnknownToken)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);

  const auto plan = Planner().MakePlan();
  (void)plan;

  auto& tracker = Planner().Tracker(material);
  const DependencyToken unknown { PlanItemId { 999U } };

  const auto result = tracker.MarkReady(unknown);

  EXPECT_FALSE(result);
  EXPECT_FALSE(tracker.IsReady());
  EXPECT_FALSE(Planner().ReadyEvent(material).ready);
}

//! Validate self-dependency is detected as a cycle.
NOLINT_TEST_F(ImportPlannerDeathTest, MakePlanSelfCycleDies)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  Planner().AddDependency(texture, texture);

  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

//! Validate disjoint subgraphs preserve registration order.
NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanDisjointOrder)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});
  const auto buffer = Planner().AddBufferResource("buffer", {});
  const auto geometry = Planner().AddGeometryAsset("geometry", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(geometry, buffer);

  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanOrderDiffersFromId)
{
  RegisterAllPipelines();

  const auto early = Planner().AddTextureResource("early", {});
  const auto middle = Planner().AddBufferResource("middle", {});
  const auto late = Planner().AddMaterialAsset("late", {});

  Planner().AddDependency(early, late);

  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, MakePlanComplexScene)
{
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

  const auto plan = Planner().MakePlan();

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
NOLINT_TEST_F(ImportPlannerPlanTest, PipelineTypeForPreSeal)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  const auto pipeline_type = Planner().PipelineTypeFor(texture);

  EXPECT_EQ(pipeline_type, MockTexturePipeline::ClassTypeId());
}

//! Validate missing pipeline registration is a blocking error.
NOLINT_TEST_F(ImportPlannerDeathTest, MakePlanMissingPipelineDies)
{
  (void)Planner().AddTextureResource("texture", {});

  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "Missing pipeline registration");
}

//! Validate cycle detection triggers a blocking error.
NOLINT_TEST_F(ImportPlannerDeathTest, MakePlanCycleDies)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(material, texture);
  Planner().AddDependency(texture, material);

  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

//! Validate upstream planning rejects cyclic asset authoring before runtime.
NOLINT_TEST_F(ImportPlannerDeathTest, MakePlanCrossAssetCycleDies)
{
  RegisterAllPipelines();

  const auto scene = Planner().AddSceneAsset("scene", {});
  const auto geometry = Planner().AddGeometryAsset("geometry", {});
  const auto material = Planner().AddMaterialAsset("material", {});

  Planner().AddDependency(scene, geometry);
  Planner().AddDependency(geometry, material);
  Planner().AddDependency(material, scene);

  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "cycle detected");
}

NOLINT_TEST_F(ImportPlannerDeathTest, AddAfterSealDies)
{
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});
  (void)Planner().MakePlan();

  NOLINT_EXPECT_DEATH(
    Planner().AddBufferResource("buffer", {}), "sealed and cannot be modified");
}

//! Validate MakePlan cannot be called twice.
NOLINT_TEST_F(ImportPlannerDeathTest, MakePlanTwiceDies)
{
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});
  (void)Planner().MakePlan();

  NOLINT_EXPECT_DEATH(Planner().MakePlan(), "sealed and cannot be modified");
}

//! Validate invalid PlanItemId access is rejected.
NOLINT_TEST_F(ImportPlannerDeathTest, InvalidItemIdDies)
{
  RegisterAllPipelines();

  (void)Planner().AddTextureResource("texture", {});

  NOLINT_EXPECT_DEATH(
    Planner().Item(PlanItemId { 42U }), "PlanItemId out of range");
}

//! Validate invalid dependency references are rejected.
NOLINT_TEST_F(ImportPlannerDeathTest, AddDependencyInvalidDies)
{
  RegisterAllPipelines();

  const auto texture = Planner().AddTextureResource("texture", {});

  NOLINT_EXPECT_DEATH(Planner().AddDependency(texture, PlanItemId { 99U }),
    "PlanItemId out of range");
}

} // namespace
