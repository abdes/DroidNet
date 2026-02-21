#include <cassert>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene {

class SceneTraversalBenchmark : public benchmark::Fixture {
public:
  void SetUp(const benchmark::State& state) override
  {
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = true;
    loguru::g_preamble_header = false;
    loguru::g_global_verbosity = loguru::Verbosity_ERROR;
    loguru::g_colorlogtostderr = true;

    // Create test scene with different hierarchies based on benchmark
    // parameters
    const int depth = static_cast<int>(state.range(0));
    const int width = static_cast<int>(state.range(1));
    const uint32_t seed = static_cast<uint32_t>(
      0xC0FFEEu ^ (depth * 73856093u) ^ (width * 19349663u));
    rng_ = std::mt19937(seed);
    static constexpr size_t kBenchmarkSceneCapacity = 4096;
    scene_ = std::make_shared<Scene>("BenchmarkScene", kBenchmarkSceneCapacity);
    rng_ = std::mt19937 { static_cast<std::mt19937::result_type>(
      (depth * 73856093) ^ (width * 19349663)) };
    CreateTestHierarchy(depth, width);

    // Note: Nodes are created clean (dirty flags cleared during creation)
    // Individual benchmarks will set their own dirty ratios using Args[2]
  }

  void TearDown(const benchmark::State& /*state*/) override
  {
    scene_.reset();
    all_nodes_.clear();
  }

protected:
  // Helper: Create a scene node with proper flags (following functional test
  // pattern)
  [[nodiscard]] auto CreateRootNode(const std::string& name,
    const glm::vec3& position = { 0.0F, 0.0F, 0.0F }) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kStatic,
                           SceneFlag {}.SetEffectiveValueBit(false));
    auto node = scene_->CreateNode(name, flags);
    assert(node.IsValid());

    // Set transform if not default
    if (position != glm::vec3 { 0.0F, 0.0F, 0.0F }) {
      auto transform = node.GetTransform();
      transform.SetLocalPosition(position);
    }

    // Update the root node world matrix to get some meaningful values.
    const auto impl = node.GetImpl();
    assert(impl.has_value());
    impl->get().UpdateTransforms(*scene_);

    return node;
  }
  void CreateChildNodes(
    SceneNode& parent, const int remaining_depth, const int children_per_node)
  {
    if (remaining_depth <= 0) {
      return;
    }

    for (int i = 0; i < children_per_node; ++i) {
      auto child_node
        = scene_->CreateChildNode(parent, "child_" + std::to_string(i));
      assert(child_node.has_value());

      auto transform = child_node->GetTransform();
      transform.SetLocalPosition(
        { static_cast<float>(i), static_cast<float>(remaining_depth), 0.0F });

      // Clear the dirty flag that was set when we modified the transform
      auto impl = child_node->GetImpl();
      assert(impl.has_value());

      all_nodes_.push_back(*child_node);

      CreateChildNodes(*child_node, remaining_depth - 1, children_per_node);
    }
  }

  void CreateTestHierarchy(const int max_depth, const int children_per_node)
  {
    if (max_depth <= 0) {
      return;
    }

    // Create root nodes
    for (int i = 0; i < children_per_node; ++i) {
      auto root_node = CreateRootNode(
        "root_" + std::to_string(i), { static_cast<float>(i), 0.0F, 0.0F });

      all_nodes_.push_back(root_node);

      CreateChildNodes(root_node, max_depth - 1, children_per_node);
    }
  }

  void MarkRandomNodesDirty(const float percentage)
  {
    std::uniform_real_distribution dis(0.0F, 1.0F);

    for (auto& node : all_nodes_) {
      if (dis(rng_) < percentage) {
        auto impl = node.GetImpl();
        assert(impl.has_value());
        // Modify transform to mark it dirty
        if (auto pos = node.GetTransform().GetLocalPosition()) {
          glm::vec3 new_pos = *pos;
          new_pos.x += 0.001F; // Small change to trigger dirty flag - NOLINT
          node.GetTransform().SetLocalPosition(new_pos);
        }
      }
    }
  }

  std::shared_ptr<Scene> scene_; // NOLINT(*-non-private-*)
  std::vector<SceneNode> all_nodes_; // NOLINT(*-non-private-*)
  std::mt19937 rng_ { 0 };
};

// Benchmark SceneTraversal visitor pattern processing ONLY DIRTY transform
// nodes
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)(
  benchmark::State& state)
{
  // Extract dirty ratio from benchmark args: args[2] = dirty ratio (0.0
  // to 1.0)
  const float dirty_ratio = static_cast<float>(state.range(2)) / 100.0F;
  int64_t total_visited_nodes = 0;

  std::uint64_t visited_nodes_accum = 0;
  for (auto _ : state) {
    state.PauseTiming();
    MarkRandomNodesDirty(dirty_ratio); // Set specified percentage dirty
    state.ResumeTiming();

    // Contract: DirtyTransformFilter requires prepared dirty subtree counts.
    SceneTraversal traversal(scene_);
    [[maybe_unused]] const auto prepared
      = traversal.PrepareDirtyFlagsAndSubtreeCounts(false);
    auto result = traversal.Traverse(
      [this](const auto& node, [[maybe_unused]] bool dry_run) -> VisitResult {
        DCHECK_F(!dry_run,
          "Benchmark uses kPreOrder and should never receive dry_run=true");
        node.node_impl->UpdateTransforms(*scene_);
        return VisitResult::kContinue; // Continue traversal
      },
      TraversalOrder::kPreOrder, DirtyTransformFilter {});

    total_visited_nodes += static_cast<int64_t>(result.nodes_visited);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(total_visited_nodes);
  state.counters["visited_nodes"]
    = benchmark::Counter(static_cast<double>(total_visited_nodes),
      benchmark::Counter::kAvgIterations);
}

// Benchmark SceneTraversal built-in UpdateTransforms() method
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, TraversalBuiltInUpdateTransforms)(
  benchmark::State& state)
{
  const float dirty_ratio = static_cast<float>(state.range(2)) / 100.0F;
  int64_t total_updated_nodes = 0;

  for (auto _ : state) {
    state.PauseTiming();
    MarkRandomNodesDirty(dirty_ratio);
    state.ResumeTiming();

    SceneTraversal traversal(scene_);
    [[maybe_unused]] const auto prepared
      = traversal.PrepareDirtyFlagsAndSubtreeCounts(false);
    const auto result = traversal.UpdateTransforms();

    total_updated_nodes += static_cast<int64_t>(result);
    benchmark::DoNotOptimize(result);
  }

  state.SetItemsProcessed(total_updated_nodes);
  state.counters["updated_nodes"]
    = benchmark::Counter(static_cast<double>(total_updated_nodes),
      benchmark::Counter::kAvgIterations);
}

// Register identical argument matrix for fair A/B comparison.
// Args format: { depth, width, dirty_percentage }
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
  ->Args({ 3, 4, 10 })
  ->Args({ 3, 4, 30 })
  ->Args({ 3, 4, 100 })
  ->Args({ 5, 3, 30 })
  ->Args({ 5, 3, 100 })
  ->Args({ 4, 6, 30 })
  ->Args({ 4, 6, 100 })
  ->Args({ 8, 2, 30 })
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalBuiltInUpdateTransforms)
  ->Args({ 3, 4, 10 })
  ->Args({ 3, 4, 30 })
  ->Args({ 3, 4, 100 })
  ->Args({ 5, 3, 30 })
  ->Args({ 5, 3, 100 })
  ->Args({ 4, 6, 30 })
  ->Args({ 4, 6, 100 })
  ->Args({ 8, 2, 30 })
  ->Unit(benchmark::kMicrosecond);

} // namespace oxygen::scene
