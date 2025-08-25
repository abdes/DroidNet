//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Scheduler.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>

#include "RenderGraph.h"

namespace oxygen::examples::asyncsim {

//! Enhanced RenderGraphScheduler with topological sorting and dependency
//! resolution
class AsyncRenderGraphScheduler : public RenderGraphScheduler {
public:
  AsyncRenderGraphScheduler() = default;

  [[nodiscard]] auto SchedulePasses(const RenderGraph& graph)
    -> SchedulingResult override
  {
    LOG_F(2, "[RenderGraphScheduler] Scheduling passes for render graph");

    SchedulingResult result;

    // Get passes and their dependencies from the graph
    std::vector<PassHandle> passes;
    passes.reserve(graph.GetPasses().size());
    for (const auto& [h, _] : graph.GetPasses())
      passes.push_back(h);
    if (passes.empty()) {
      LOG_F(WARNING, "[RenderGraphScheduler] No passes to schedule");
      return result;
    }

    // Build dependency graph (explicit only for now)
    const auto dependency_graph = BuildDependencyGraph(graph);

    // Perform topological sort (base order satisfies dependencies)
    result.execution_order = TopologicalSort(passes, dependency_graph);

    if (result.execution_order.empty()) {
      LOG_F(ERROR,
        "[RenderGraphScheduler] Topological sort failed - circular dependency "
        "detected");
      return result;
    }

    // Cost-aware refinement: within each dependency level, reorder by updated
    // cost
    CostAwareRefine(graph, dependency_graph, result.execution_order);

    // Assign queues based on pass types and dependencies (will later also
    // consider cost)
    result.queue_assignments
      = AssignQueues(result.execution_order, dependency_graph);

    // Estimate frame time (use profiler metrics if available)
    result.estimated_frame_time_ms
      = EstimateFrameTime(result.execution_order, graph);

    LOG_F(2,
      "[RenderGraphScheduler] Scheduled {} passes with estimated frame time: "
      "{:.2f}ms",
      result.execution_order.size(), result.estimated_frame_time_ms);

    if (loguru::g_stderr_verbosity >= 3) {
      for (size_t i = 0; i < result.execution_order.size(); ++i) {
        const auto handle = result.execution_order[i];
        const auto q = i < result.queue_assignments.size()
          ? result.queue_assignments[i]
          : QueueType::Graphics;
        const char* qname = q == QueueType::Graphics
          ? "Gfx"
          : (q == QueueType::Compute ? "Cmp" : "Cpy");
        LOG_F(3, "[RenderGraphScheduler]   [{}] pass={} queue={}", i,
          handle.get(), qname);
      }
    }

    return result;
  }

  [[nodiscard]] auto AnalyzeCriticalPath(const RenderGraph& graph)
    -> std::vector<PassHandle> override
  {
    LOG_F(3, "[RenderGraphScheduler] Analyzing critical path");

    std::vector<PassHandle> passes;
    passes.reserve(graph.GetPasses().size());
    for (const auto& [h, _] : graph.GetPasses())
      passes.push_back(h);
    const auto dependency_graph = BuildDependencyGraph(graph);

    // Calculate longest path from each node (critical path analysis)
    std::unordered_map<PassHandle, float> longest_path;
    std::vector<PassHandle> critical_path;

    // Initialize all paths to 0
    for (const auto& pass : passes) {
      longest_path[pass] = 0.0f;
    }

    // Perform topological sort to process nodes in correct order
    const auto sorted_passes = TopologicalSort(passes, dependency_graph);

    // Calculate longest paths using dynamic programming
    for (const auto& pass : sorted_passes) {
      const auto pass_cost = GetPassCost(pass);

      // Check all dependencies and find the longest path to this node
      const auto deps_it = dependency_graph.find(pass);
      if (deps_it != dependency_graph.end()) {
        for (const auto& dep : deps_it->second) {
          longest_path[pass] = std::max(longest_path[pass],
            longest_path[dep] + static_cast<float>(pass_cost.cpu_us));
        }
      } else {
        longest_path[pass] = static_cast<float>(pass_cost.cpu_us);
      }
    }

    // Find the pass with the longest total path (end of critical path)
    auto max_it = std::max_element(longest_path.begin(), longest_path.end(),
      [](const auto& a, const auto& b) { return a.second < b.second; });

    if (max_it != longest_path.end()) {
      // Reconstruct critical path by backtracking
      critical_path = ReconstructCriticalPath(
        max_it->first, dependency_graph, longest_path);
    }

    LOG_F(3,
      "[RenderGraphScheduler] Critical path contains {} passes with total "
      "time: {:.2f}ms",
      critical_path.size(),
      max_it != longest_path.end() ? max_it->second : 0.0f);

    return critical_path;
  }

  auto OptimizeMultiQueue(SchedulingResult& result) -> void override
  {
    LOG_F(3, "[RenderGraphScheduler] Optimizing for multi-queue execution");

    if (result.execution_order.empty()) {
      return;
    }

    // Reassign queues to maximize parallelism
    const auto num_passes = result.execution_order.size();
    if (result.queue_assignments.size() != num_passes) {
      result.queue_assignments.resize(num_passes, QueueType::Graphics);
    }

    // Simple optimization: assign compute passes to compute queue when possible
    for (size_t i = 0; i < num_passes; ++i) {
      const auto& pass = result.execution_order[i];
      const auto pass_cost = GetPassCost(pass);

      // If this is a compute-heavy pass and compute queue is available
      if (pass_cost.gpu_us > pass_cost.cpu_us * 2.0f) {
        result.queue_assignments[i] = QueueType::Compute;
      }

      // Interleave copy operations on the copy queue
      if (pass_cost.memory_bytes
        > 100 * 1024 * 1024) { // Large memory operations (100MB)
        result.queue_assignments[i] = QueueType::Copy;
      }
    }

    LOG_F(3, "[RenderGraphScheduler] Multi-queue optimization complete");
    if (loguru::g_stderr_verbosity >= 4) {
      LOG_F(4, "[RenderGraphScheduler] Final execution order with queues:");
      for (size_t i = 0; i < result.execution_order.size(); ++i) {
        const auto q = i < result.queue_assignments.size()
          ? result.queue_assignments[i]
          : QueueType::Graphics;
        const char* qname = q == QueueType::Graphics
          ? "Gfx"
          : (q == QueueType::Compute ? "Cmp" : "Cpy");
        LOG_F(4, "[RenderGraphScheduler]   [{}] pass={} queue={}", i,
          result.execution_order[i].get(), qname);
      }
    }
  }

private:
  std::unordered_map<std::string, Priority> pass_type_priorities_;
  std::unordered_map<PassHandle, PassMetrics> pass_metrics_;

  //! Perform cost-aware refinement of topological order in-place.
  void CostAwareRefine(const RenderGraph& graph,
    const std::unordered_map<PassHandle, std::vector<PassHandle>>& deps,
    std::vector<PassHandle>& order) const
  {
    const auto& profiler = graph.GetPassCostProfiler();
    if (!profiler || order.empty())
      return; // nothing to do
    // Build reverse adjacency (dependents) to compute levels (distance from
    // roots)
    std::unordered_map<PassHandle, int> in_degree;
    for (auto h : order)
      in_degree[h] = 0;
    for (auto& [p, dlist] : deps) {
      for (auto d : dlist) {
        (void)d;
        in_degree[p]++;
      }
    }
    // Level assignment via Kahn-like traversal
    std::queue<PassHandle> q;
    for (auto& [p, deg] : in_degree)
      if (deg == 0)
        q.push(p);
    std::unordered_map<PassHandle, uint32_t> level;
    while (!q.empty()) {
      auto cur = q.front();
      q.pop();
      uint32_t cur_level = level[cur];
      // Find dependents: any pass listing cur as dependency
      for (auto& [p, dlist] : deps) {
        if (std::find(dlist.begin(), dlist.end(), cur) != dlist.end()) {
          if (--in_degree[p] == 0) {
            level[p] = cur_level + 1;
            q.push(p);
          }
        }
      }
    }
    // Group passes by level preserving existing relative order, then sort each
    // group by cost desc (gpu then cpu)
    std::unordered_map<uint32_t, std::vector<PassHandle>> groups;
    uint32_t max_level = 0;
    for (auto h : order) {
      auto lv_it = level.find(h);
      uint32_t lv = lv_it != level.end() ? lv_it->second : 0;
      max_level = std::max(max_level, lv);
      groups[lv].push_back(h);
    }
    auto cost_key = [&](PassHandle h) {
      auto cost = profiler->GetUpdatedCost(h);
      return std::pair<uint32_t, uint32_t> { cost.gpu_us, cost.cpu_us };
    };
    for (uint32_t lv = 0; lv <= max_level; ++lv) {
      auto it = groups.find(lv);
      if (it == groups.end())
        continue;
      auto& vec = it->second;
      std::stable_sort(vec.begin(), vec.end(), [&](PassHandle a, PassHandle b) {
        auto ca = cost_key(a);
        auto cb = cost_key(b);
        if (ca.first != cb.first)
          return ca.first > cb.first; // gpu desc
        if (ca.second != cb.second)
          return ca.second > cb.second; // cpu desc
        return a.get() < b.get();
      });
    }
    // Rebuild order concatenating groups in level order
    std::vector<PassHandle> refined;
    refined.reserve(order.size());
    for (uint32_t lv = 0; lv <= max_level; ++lv) {
      auto it = groups.find(lv);
      if (it == groups.end())
        continue;
      refined.insert(refined.end(), it->second.begin(), it->second.end());
    }
    order.swap(refined);
    if (loguru::g_stderr_verbosity >= 4) {
      LOG_F(4, "[RenderGraphScheduler] Cost-aware refined order:");
      for (size_t i = 0; i < order.size(); ++i) {
        auto c = profiler->GetUpdatedCost(order[i]);
        LOG_F(4, "  [{}] pass={} gpu={}us cpu={}us", i, order[i].get(),
          c.gpu_us, c.cpu_us);
      }
    }
  }

  //! Build dependency graph from render graph
  [[nodiscard]] auto BuildDependencyGraph(const RenderGraph& graph) const
    -> std::unordered_map<PassHandle, std::vector<PassHandle>>
  {
    // Start with explicit dependency graph provided by builder
    auto dependency_graph = graph.GetExplicitDependencies();

    // Ensure all passes exist as keys
    for (const auto& [handle, pass_ptr] : graph.GetPasses()) {
      dependency_graph.try_emplace(handle);
    }

    // Add resource hazard based edges (write->read, write->write) to enforce
    // ordering. Deterministic ordering achieved by iterating passes sorted by
    // handle id.
    std::vector<PassHandle> sorted;
    sorted.reserve(graph.GetPasses().size());
    for (const auto& [h, _] : graph.GetPasses())
      sorted.push_back(h);
    std::sort(sorted.begin(), sorted.end(),
      [](auto a, auto b) { return a.get() < b.get(); });

    // Track last writer per resource
    std::unordered_map<ResourceHandle, PassHandle> last_writer;

    for (const auto& pass_handle : sorted) {
      const auto* pass = graph.GetPass(pass_handle);
      if (!pass)
        continue;

      // Reads depend on last writer
      for (const auto& r : pass->GetReadResources()) {
        auto it = last_writer.find(r);
        if (it != last_writer.end() && it->second != pass_handle) {
          auto& deps = dependency_graph[pass_handle];
          if (std::find(deps.begin(), deps.end(), it->second) == deps.end()) {
            deps.push_back(it->second);
          }
        }
      }
      // Writes depend on last writer then become new writer
      for (const auto& w : pass->GetWriteResources()) {
        auto it = last_writer.find(w);
        if (it != last_writer.end() && it->second != pass_handle) {
          auto& deps = dependency_graph[pass_handle];
          if (std::find(deps.begin(), deps.end(), it->second) == deps.end()) {
            deps.push_back(it->second);
          }
        }
        last_writer[w] = pass_handle;
      }
    }
    return dependency_graph;
  }

  //! Perform topological sort using Kahn's algorithm
  [[nodiscard]] auto TopologicalSort(const std::vector<PassHandle>& passes,
    const std::unordered_map<PassHandle, std::vector<PassHandle>>&
      dependency_graph) const -> std::vector<PassHandle>
  {
    // Calculate in-degrees for all nodes
    std::unordered_map<PassHandle, int> in_degree;
    for (const auto& pass : passes) {
      in_degree[pass] = 0;
    }

    for (const auto& [pass, deps] : dependency_graph) {
      in_degree[pass] += static_cast<int>(deps.size());
    }

    // Find all nodes with no incoming edges
    std::queue<PassHandle> zero_in_degree_queue;
    for (const auto& [pass, degree] : in_degree) {
      if (degree == 0) {
        zero_in_degree_queue.push(pass);
      }
    }

    std::vector<PassHandle> sorted_order;

    // Kahn's algorithm
    while (!zero_in_degree_queue.empty()) {
      const auto current = zero_in_degree_queue.front();
      zero_in_degree_queue.pop();
      sorted_order.push_back(current);

      // Find all passes that depend on the current pass
      for (const auto& [pass, deps] : dependency_graph) {
        const auto it = std::find(deps.begin(), deps.end(), current);
        if (it != deps.end()) {
          in_degree[pass]--;
          if (in_degree[pass] == 0) {
            zero_in_degree_queue.push(pass);
          }
        }
      }
    }

    // Check for cycles
    if (sorted_order.size() != passes.size()) {
      LOG_F(ERROR,
        "[RenderGraphScheduler] Circular dependency detected in pass graph");
      return {}; // Return empty vector to indicate failure
    }

    return sorted_order;
  }

  //! Assign queue types to passes
  [[nodiscard]] auto AssignQueues(
    const std::vector<PassHandle>& execution_order,
    const std::unordered_map<PassHandle, std::vector<PassHandle>>&
      dependency_graph) const -> std::vector<QueueType>
  {
    (void)dependency_graph; // future: refine with edge-based latency hiding
    std::vector<QueueType> assignments;
    assignments.reserve(execution_order.size());

    // Accumulated predicted finish times for each queue (ms)
    float gfx_time = 0.f, cmp_time = 0.f, cpy_time = 0.f;
    // We do not have pass type metadata; infer from relative cost
    // characteristics using updated profiler cost (gpu vs cpu) and memory size.
    auto classify = [&](const PassCost& c) {
      const bool copy_like
        = c.memory_bytes > 8 * 1024 * 1024 && c.gpu_us < c.cpu_us * 2;
      const bool compute_like = c.gpu_us > c.cpu_us * 2;
      if (copy_like)
        return QueueType::Copy;
      if (compute_like)
        return QueueType::Compute;
      return QueueType::Graphics;
    };

    // (Removed unused graph_ptr variable)

    for (auto h : execution_order) {
      // Derive static cost approximation
      auto static_cost = GetPassCost(h);
      // Heuristic classification
      auto preferred = classify(static_cost);
      // Load balancing: choose least loaded among eligible (preferred or
      // fallback to graphics)
      QueueType chosen = preferred;
      switch (preferred) {
      case QueueType::Graphics: {
        // Optionally move to compute if gfx more saturated and compute would
        // not violate classification
        float min_time = gfx_time;
        chosen = QueueType::Graphics;
        if (static_cost.gpu_us > static_cost.cpu_us * 1.5f
          && cmp_time < min_time) {
          min_time = cmp_time;
          chosen = QueueType::Compute;
        }
        if (static_cost.memory_bytes > 16 * 1024 * 1024
          && cpy_time < min_time) {
          min_time = cpy_time;
          chosen = QueueType::Copy;
        }
        break;
      }
      case QueueType::Compute: {
        // If compute queue heavily loaded relative to graphics, spill
        float predicted_cmp
          = cmp_time + static_cast<float>(static_cost.gpu_us) / 1000.0f;
        if (predicted_cmp > gfx_time * 1.2f) {
          chosen = QueueType::Graphics;
        }
        break;
      }
      case QueueType::Copy: {
        // Large memory ops stay; small ones can merge into graphics
        if (static_cost.memory_bytes < 4 * 1024 * 1024) {
          chosen = QueueType::Graphics;
        }
        break;
      }
      }
      // Update accumulators
      const float dur_ms = std::max(static_cast<float>(static_cost.cpu_us),
                             static_cast<float>(static_cost.gpu_us))
        / 1000.0f;
      if (chosen == QueueType::Graphics)
        gfx_time += dur_ms;
      else if (chosen == QueueType::Compute)
        cmp_time += dur_ms;
      else
        cpy_time += dur_ms;
      assignments.push_back(chosen);
    }

    if (loguru::g_stderr_verbosity >= 4) {
      LOG_F(4,
        "[RenderGraphScheduler] Queue load summary gfx={:.3f}ms cmp={:.3f}ms "
        "cpy={:.3f}ms",
        gfx_time, cmp_time, cpy_time);
    }

    return assignments;
  }

  //! Estimate total frame time (prefer profiler averages when available)
  [[nodiscard]] auto EstimateFrameTime(
    const std::vector<PassHandle>& execution_order,
    const RenderGraph& graph) const -> float
  {
    float total_time_ms = 0.0f;
    const auto& profiler = graph.GetPassCostProfiler();
    for (const auto& pass : execution_order) {
      float pass_time_ms = 0.0f;
      bool used_profiler = false;
      if (profiler) {
        // In a fuller implementation profiler would expose avg CPU/GPU; here we
        // fall back since PassCostProfiler stub doesn't yet provide retrieval
        // API.
      }
      if (!used_profiler) {
        const auto pass_cost = GetPassCost(pass);
        const auto cpu_time_ms = static_cast<float>(pass_cost.cpu_us) / 1000.0f;
        const auto gpu_time_ms = static_cast<float>(pass_cost.gpu_us) / 1000.0f;
        pass_time_ms = std::max(cpu_time_ms, gpu_time_ms);
        LOG_F(4,
          "[RenderGraphScheduler] Pass {} estimated cost: CPU={}us GPU={}us -> "
          "{:.3f} ms",
          pass.get(), pass_cost.cpu_us, pass_cost.gpu_us, pass_time_ms);
      }
      total_time_ms += pass_time_ms;
    }
    LOG_F(3, "[RenderGraphScheduler] Aggregate estimated frame time: {:.3f} ms",
      total_time_ms);
    return total_time_ms;
  }

  //! Get pass cost metrics
  [[nodiscard]] auto GetPassCost(PassHandle pass) const -> PassCost
  {
    // Simple cost estimation based on pass ID
    // In real implementation, would use historical metrics or static analysis
    const auto pass_id = pass.get();

    PassCost cost;
    cost.cpu_us = static_cast<uint32_t>(100 + (pass_id % 10) * 50); // 100-550μs
    cost.gpu_us
      = static_cast<uint32_t>(500 + (pass_id % 8) * 200); // 500-1900μs
    cost.memory_bytes
      = static_cast<uint32_t>(10240 + (pass_id % 20) * 5120); // 10-105KB

    return cost;
  }

  //! Reconstruct critical path by backtracking
  [[nodiscard]] auto ReconstructCriticalPath(PassHandle end_pass,
    const std::unordered_map<PassHandle, std::vector<PassHandle>>&
      dependency_graph,
    const std::unordered_map<PassHandle, float>& longest_path) const
    -> std::vector<PassHandle>
  {
    std::vector<PassHandle> critical_path;
    PassHandle current = end_pass;

    while (true) {
      critical_path.push_back(current);

      // Find the dependency that leads to the longest path
      const auto deps_it = dependency_graph.find(current);
      if (deps_it == dependency_graph.end() || deps_it->second.empty()) {
        break; // No more dependencies
      }

      PassHandle best_dep { 0 };
      float best_path_length = -1.0f;

      for (const auto& dep : deps_it->second) {
        const auto path_it = longest_path.find(dep);
        if (path_it != longest_path.end()
          && path_it->second > best_path_length) {
          best_path_length = path_it->second;
          best_dep = dep;
        }
      }

      if (best_path_length >= 0.0f) {
        current = best_dep;
      } else {
        break;
      }
    }

    // Reverse to get path from start to end
    std::reverse(critical_path.begin(), critical_path.end());
    return critical_path;
  }
};

//! Factory function to create AsyncEngine-integrated scheduler
auto CreateAsyncRenderGraphScheduler() -> std::unique_ptr<RenderGraphScheduler>
{
  return std::make_unique<AsyncRenderGraphScheduler>();
}

} // namespace oxygen::examples::asyncsim
