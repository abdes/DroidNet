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
#include <Oxygen/Scene/TransformComponent.h>

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
        int depth = static_cast<int>(state.range(0));
        int width = static_cast<int>(state.range(1));
        scene = std::make_shared<Scene>("BenchmarkScene", 4096);
        CreateTestHierarchy(depth, width);

        // Note: Nodes are created clean (dirty flags cleared during creation)
        // Individual benchmarks will set their own dirty ratios using Args[2]
    }

    void TearDown(const benchmark::State& /*state*/) override
    {
        scene.reset();
        allNodes.clear();
    }

protected:
    // Helper: Create a scene node with proper flags (following functional test pattern)
    auto CreateRootNode(const std::string& name, const glm::vec3& position = { 0.0f, 0.0f, 0.0f }) -> SceneNode
    {
        auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible, SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kStatic, SceneFlag {}.SetEffectiveValueBit(false));
        auto node = scene->CreateNode(name, flags);
        assert(node.IsValid());

        // Set transform if not default
        if (position != glm::vec3 { 0.0f, 0.0f, 0.0f }) {
            auto transform = node.GetTransform();
            transform.SetLocalPosition(position);
        }

        // Update the root node world matrix to get some meaningful values.
        auto impl = node.GetObject();
        assert(impl.has_value());
        impl->get().UpdateTransforms(*scene);

        return node;
    }
    void CreateChildNodes(SceneNode parent, int remainingDepth, int childrenPerNode)
    {
        if (remainingDepth <= 0)
            return;

        for (int i = 0; i < childrenPerNode; ++i) {
            auto childNode = scene->CreateChildNode(parent, "child_" + std::to_string(i));
            assert(childNode.has_value());

            auto transform = childNode->GetTransform();
            transform.SetLocalPosition({ static_cast<float>(i), static_cast<float>(remainingDepth), 0.0f });

            // Clear the dirty flag that was set when we modified the transform
            auto impl = childNode->GetObject();
            assert(impl.has_value());
            impl->get().ClearTransformDirty();

            allNodes.push_back(*childNode);

            CreateChildNodes(*childNode, remainingDepth - 1, childrenPerNode);
        }
    }

    void CreateTestHierarchy(int maxDepth, int childrenPerNode)
    {
        if (maxDepth <= 0) {
            return;
        }

        // Create root nodes
        for (int i = 0; i < childrenPerNode; ++i) {
            auto rootNode = CreateRootNode(
                "root_" + std::to_string(i),
                { static_cast<float>(i), 0.0f, 0.0f });

            allNodes.push_back(rootNode);

            CreateChildNodes(rootNode, maxDepth - 1, childrenPerNode);
        }
    }

    void MarkRandomNodesDirty(float percentage)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);

        for (auto& node : allNodes) {
            if (dis(gen) < percentage) {
                auto impl = node.GetObject();
                assert(impl.has_value());
                auto& transform = impl->get().GetComponent<TransformComponent>();
                // Modify transform to mark it dirty
                auto pos = transform.GetLocalPosition();
                glm::vec3 newPos = pos;
                newPos.x += 0.001f; // Small change to trigger dirty flag
                transform.SetLocalPosition(newPos);
            }
        }
    }

    std::shared_ptr<Scene> scene;
    std::vector<SceneNode> allNodes;
};

// Benchmark the Scene::Update method (existing UpdateTransformsIterative approach)
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, SceneUpdateMethod)(benchmark::State& state)
{
    // Extract dirty ratio from benchmark args: args[2] = dirty ratio (0.0 to 1.0)
    float dirty_ratio = static_cast<float>(state.range(2)) / 100.0f;

    for (auto _ : state) {
        state.PauseTiming();
        MarkRandomNodesDirty(dirty_ratio); // Set specified percentage dirty
        state.ResumeTiming();

        // Use the Scene's built-in update method with disableDirtyFlagsUpdate=true
        // This calls the existing UpdateTransformsIterative implementation
        scene->Update(true);

        benchmark::DoNotOptimize(scene.get());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * allNodes.size());
}

// Benchmark SceneTraversal visitor pattern processing ONLY DIRTY transform nodes
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)(benchmark::State& state)
{
    // Extract dirty ratio from benchmark args: args[2] = dirty ratio (0.0 to 1.0)
    float dirty_ratio = static_cast<float>(state.range(2)) / 100.0f;

    for (auto _ : state) {
        state.PauseTiming();
        MarkRandomNodesDirty(dirty_ratio); // Set specified percentage dirty
        state.ResumeTiming();        // Use optimized batch processing with dirty transform filter
        SceneTraversal traversal(*scene);
        auto result = traversal.Traverse(
            [](SceneNodeImpl& node, const Scene& scene_param) -> VisitResult {
                node.UpdateTransforms(const_cast<Scene&>(scene_param));
                return VisitResult::kContinue; // Continue traversal
            },
            TraversalOrder::kDepthFirst,
            DirtyTransformFilter {});

        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * allNodes.size());
}

// COMMENTED OUT - Helper method investigation complete
/*
// Benchmark SceneTraversal dedicated UpdateTransforms method (optimized for transform updates)
BENCHMARK_DEFINE_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)(benchmark::State& state)
{
    // Extract dirty ratio from benchmark args: args[2] = dirty ratio (0.0 to 1.0)
    float dirty_ratio = static_cast<float>(state.range(2)) / 100.0f;

    for (auto _ : state) {
        state.PauseTiming();
        MarkRandomNodesDirty(dirty_ratio); // Set specified percentage dirty
        state.ResumeTiming();        // Use optimized batch transform update
        SceneTraversal traversal(*scene);
        auto updated_count = traversal.UpdateTransforms();

        benchmark::DoNotOptimize(updated_count);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * allNodes.size());
}
*/

// Register benchmarks grouped by scene configuration for easy comparison
// Args format: { depth, width, dirty_percentage }
// dirty_percentage: 30 = 30% dirty (realistic scenario)

// Small tree: depth=3, width=4 (85 nodes total) - 30% dirty
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 3, 4, 30 }) // 30% dirty (realistic scenario)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 3, 4, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Medium tree: depth=5, width=3 (364 nodes total) - 30% dirty
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 5, 3, 30 }) // 30% dirty (realistic)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Large wide tree: depth=4, width=6 (1555 nodes total) - 30% dirty
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 4, 6, 30 }) // 30% dirty (realistic)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 4, 6, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Deep narrow tree: depth=8, width=2 (511 nodes total) - 30% dirty
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 8, 2, 30 }) // 30% dirty (realistic)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 8, 2, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// COMMENTED OUT FOR FOCUSED INVESTIGATION - Helper method comparison complete
/*
// FOCUSED INVESTIGATION: Only testing TraversalVisitor vs TraversalHelper on medium tree with 100% dirty
// Medium tree: depth=5, width=3 (364 nodes total) - 100% dirty (worst case)

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)
    ->Args({ 5, 3, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// COMMENTED OUT FOR FOCUSED INVESTIGATION
/*
// Small tree: depth=3, width=4 (85 nodes total)
// Test different dirty ratios for comparison
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 3, 4, 10 }) // 10% dirty (light updates)
    ->Args({ 3, 4, 30 }) // 30% dirty (realistic scenario)
    ->Args({ 3, 4, 100 }) // 100% dirty (worst case)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 3, 4, 10 }) // 10% dirty
    ->Args({ 3, 4, 30 }) // 30% dirty
    ->Args({ 3, 4, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)
    ->Args({ 3, 4, 10 }) // 10% dirty
    ->Args({ 3, 4, 30 }) // 30% dirty
    ->Args({ 3, 4, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// Medium tree: depth=5, width=3 (364 nodes total)
// Focus on realistic scenarios for medium tree
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 5, 3, 30 }) // 30% dirty (realistic)
    ->Args({ 5, 3, 100 }) // 100% dirty (worst case)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 5, 3, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)
    ->Args({ 5, 3, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

// Large wide tree: depth=4, width=6 (1555 nodes total)
// Test scalability with large trees
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 4, 6, 30 }) // 30% dirty (realistic)
    ->Args({ 4, 6, 100 }) // 100% dirty (stress test)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 4, 6, 30 }) // 30% dirty
    ->Args({ 4, 6, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)
    ->Args({ 4, 6, 30 }) // 30% dirty
    ->Args({ 4, 6, 100 }) // 100% dirty
    ->Unit(benchmark::kMicrosecond);

// Deep narrow tree: depth=8, width=2 (511 nodes total)
// Test deep hierarchy performance
BENCHMARK_REGISTER_F(SceneTraversalBenchmark, SceneUpdateMethod)
    ->Args({ 8, 2, 30 }) // 30% dirty (realistic)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalVisitorUpdateTransforms)
    ->Args({ 8, 2, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(SceneTraversalBenchmark, TraversalUpdateTransformsHelper)
    ->Args({ 8, 2, 30 }) // 30% dirty
    ->Unit(benchmark::kMicrosecond);
*/

} // namespace oxygen::scene
