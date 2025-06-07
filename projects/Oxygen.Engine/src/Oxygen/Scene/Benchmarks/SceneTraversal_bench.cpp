#include <benchmark/benchmark.h>
#include <cassert>
#include <memory>
#include <random>
#include <vector>

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
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
        loguru::g_colorlogtostderr = true;

        // Create test scene with different hierarchies based on benchmark parameters
        const int depth = static_cast<int>(state.range(0));
        const int width = static_cast<int>(state.range(1));
        scene_ = std::make_shared<Scene>("BenchmarkScene", 4096);
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
    // Helper: Create a scene node with proper flags (following functional test pattern)
    [[nodiscard]] auto CreateRootNode(
        const std::string& name,
        const glm::vec3& position = { 0.0f, 0.0f, 0.0f }) const
        -> SceneNode
    {
        const auto flags = SceneNode::Flags {}
                               .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
                               .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(false));
        auto node = scene_->CreateNode(name, flags);
        assert(node.IsValid());

        // Set transform if not default
        if (position != glm::vec3 { 0.0f, 0.0f, 0.0f }) {
            auto transform = node.GetTransform();
            transform.SetLocalPosition(position);
        }

        // Update the root node world matrix to get some meaningful values.
        const auto impl = node.GetObject();
        assert(impl.has_value());
        impl->get().UpdateTransforms(*scene_);

        return node;
    }
    void CreateChildNodes(const SceneNode& parent, const int remaining_depth, const int children_per_node)
    {
        if (remaining_depth <= 0)
            return;

        for (int i = 0; i < children_per_node; ++i) {
            auto child_node = scene_->CreateChildNode(parent, "child_" + std::to_string(i));
            assert(child_node.has_value());

            auto transform = child_node->GetTransform();
            transform.SetLocalPosition({ static_cast<float>(i), static_cast<float>(remaining_depth), 0.0f });

            // Clear the dirty flag that was set when we modified the transform
            auto impl = child_node->GetObject();
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
                "root_" + std::to_string(i),
                { static_cast<float>(i), 0.0f, 0.0f });

            all_nodes_.push_back(root_node);

            CreateChildNodes(root_node, max_depth - 1, children_per_node);
        }
    }

    void MarkRandomNodesDirty(const float percentage)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution dis(0.0f, 1.0f);

        for (auto& node : all_nodes_) {
            if (dis(gen) < percentage) {
                auto impl = node.GetObject();
                assert(impl.has_value());
                // Modify transform to mark it dirty
                if (auto pos = node.GetTransform().GetLocalPosition()) {
                    glm::vec3 new_pos = *pos;
                    new_pos.x += 0.001f; // Small change to trigger dirty flag
                    node.GetTransform().SetLocalPosition(new_pos);
                }
            }
        }
    }

    std::shared_ptr<Scene> scene_;
    std::vector<SceneNode> all_nodes_;
};

// Benchmark SceneTraversal visitor pattern processing ONLY DIRTY transform nodes
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)(benchmark::State& state)
{
    // Extract dirty ratio from benchmark args: args[2] = dirty ratio (0.0 to 1.0)
    const float dirty_ratio = static_cast<float>(state.range(2)) / 100.0f;

    for (auto _ : state) {
        state.PauseTiming();
        MarkRandomNodesDirty(dirty_ratio); // Set specified percentage dirty
        state.ResumeTiming(); // Use optimized batch processing with dirty transform filter
        SceneTraversal traversal(*scene_);
        auto result = traversal.Traverse(
            [](SceneNodeImpl& node, const Scene& scene_param) -> VisitResult {
                node.UpdateTransforms(scene_param);
                return VisitResult::kContinue; // Continue traversal
            },
            TraversalOrder::kDepthFirst,
            DirtyTransformFilter {});

        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(all_nodes_.size()));
}

// Register benchmarks grouped by scene configuration for easy comparison
// Args format: { depth, width, dirty_percentage }
// dirty_percentage: 30 = 30% dirty (realistic scenario)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 3, 4, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 4, 6, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 8, 2, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Medium tree: depth=5, width=3 (364 nodes total) - 100% dirty (worst case)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// Small tree: depth=3, width=4 (85 nodes total)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 3, 4, 10 }) // 10% dirty
    ->Args({ 3, 4, 30 }) // 30% dirty
    ->Args({ 3, 4, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// Medium tree: depth=5, width=3 (364 nodes total)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Large wide tree: depth=4, width=6 (1555 nodes total)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 4, 6, 30 }) // 30% dirty
    ->Args({ 4, 6, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// Deep narrow tree: depth=8, width=2 (511 nodes total)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 8, 2, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

} // namespace oxygen::scene
