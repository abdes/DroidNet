//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraph.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Algorithms.h>

#include "../Integration/GraphicsLayerIntegration.h"
#include "ExecutionContext.h"
#include "Resource.h"
#include "Scheduler.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

//! Pass execution batch for parallelism
struct PassBatch {
  std::vector<PassHandle> passes;
  QueueType queue_type;
  bool can_execute_parallel;
  std::vector<size_t> view_indices; // Views this batch applies to

  PassBatch(QueueType queue = QueueType::Graphics)
    : queue_type(queue)
    , can_execute_parallel(false)
  {
  }
};

// Simplified AsyncEngineRenderGraph overrides
auto AsyncEngineRenderGraph::Execute(ModuleContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] Async execute");
  LOG_F(2, "{} passes", passes_.size());
  co_await PlanResourceTransitions(context);
  co_await ExecutePassBatches(context);
  co_await PresentResults(context);
  co_return;
}

auto AsyncEngineRenderGraph::PlanResourceTransitions(ModuleContext& context)
  -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] PlanResourceTransitions");
  (void)context;
  LOG_F(3,
    "[RenderGraph] PlanResourceTransitions begin ({} resources, {} passes)",
    resource_descriptors_.size(), passes_.size());

  // Reset tracker for new frame
  resource_state_tracker_.Reset();

  // Set initial states (assume Undefined unless explicitly persistent)
  for (const auto& [handle, desc] : resource_descriptors_) {
    // FrameLocal resources start Undefined; Persistent could retain state
    // (future)
    resource_state_tracker_.SetInitialState(
      handle, ResourceState::Undefined, 0);
  }

  const bool multiview = IsMultiViewEnabled() && !frame_context_.views.empty();
  const auto& order
    = execution_order_.empty() ? GetPassHandles() : execution_order_;
  for (auto handle : order) {
    auto* pass = GetPassMutable(handle);
    if (!pass)
      continue;
    const auto view_index = pass->GetViewIndex();
    const auto& reads = pass->GetReadResources();
    const auto& read_states = pass->GetReadStates();
    for (size_t i = 0; i < reads.size(); ++i) {
      resource_state_tracker_.RequestTransition(
        reads[i], read_states[i], handle, view_index);
    }
    const auto& writes = pass->GetWriteResources();
    const auto& write_states = pass->GetWriteStates();
    for (size_t i = 0; i < writes.size(); ++i) {
      resource_state_tracker_.RequestTransition(
        writes[i], write_states[i], handle, view_index);
    }
    if (multiview && pass->GetScope() == PassScope::PerView) {
      // For per-view passes, replicate transitions for each view.
      for (uint32_t vi = 1; vi < frame_context_.views.size(); ++vi) {
        for (size_t i = 0; i < reads.size(); ++i) {
          resource_state_tracker_.RequestTransition(
            reads[i], read_states[i], handle, vi);
        }
        for (size_t i = 0; i < writes.size(); ++i) {
          resource_state_tracker_.RequestTransition(
            writes[i], write_states[i], handle, vi);
        }
      }
    }
  }

  const auto& planned = resource_state_tracker_.GetPlannedTransitions();
  LOG_F(3, "[RenderGraph] Planned {} resource transitions", planned.size());
  co_return;
}

auto AsyncEngineRenderGraph::ExecutePassBatches(ModuleContext& context)
  -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] ExecutePassBatches");
  (void)context;
  LOG_F(3, "[RenderGraph] ExecutePassBatches begin");

  // Build batches (level sets) from dependency graph in scheduling result.
  // We only have explicit dependencies stored; create inverse indegree counts
  // using execution order for determinism.
  const auto& order
    = execution_order_.empty() ? GetPassHandles() : execution_order_;
  std::unordered_map<PassHandle, int> remaining_deps;
  for (auto h : order)
    remaining_deps[h] = 0;
  for (const auto& [pass, deps] : explicit_dependencies_) {
    auto it = remaining_deps.find(pass);
    if (it == remaining_deps.end())
      continue;
    it->second += static_cast<int>(deps.size());
  }

  // --- Diagnostics instrumentation (verbose level 5+) --------------------
  if (loguru::g_stderr_verbosity >= 5) {
    // Edge / degree analysis
    size_t edge_count = 0;
    std::unordered_map<PassHandle, int> out_degree;
    for (auto h : order) {
      out_degree[h] = 0;
    }
    for (const auto& [pass, deps] : explicit_dependencies_) {
      edge_count += deps.size();
      for (auto dep : deps) {
        // dep -> pass (i.e. pass depends on dep) so out_degree[dep]++
        auto it = out_degree.find(dep);
        if (it != out_degree.end())
          ++it->second;
      }
    }

    // Detailed dependency dump & validation
    size_t valid_edges = 0;
    size_t invalid_edges = 0;
    size_t missing_sources = 0;
    size_t missing_targets = 0;
    const size_t max_dump = 32; // avoid log spam
    size_t dumped = 0;
    auto in_order = [&](PassHandle h) {
      return std::find(order.begin(), order.end(), h) != order.end();
    };
    for (const auto& [pass, deps] : explicit_dependencies_) {
      bool target_ok = in_order(pass);
      if (!target_ok)
        ++missing_targets;
      for (auto dep : deps) {
        bool source_ok = in_order(dep);
        if (!source_ok)
          ++missing_sources;
        if (source_ok && target_ok) {
          ++valid_edges;
          if (dumped < max_dump) {
            LOG_F(5, "[RenderGraph][Deps] valid dep source={} -> target={}",
              dep.get(), pass.get());
            ++dumped;
          }
        } else {
          ++invalid_edges;
          if (dumped < max_dump) {
            LOG_F(5,
              "[RenderGraph][Deps][INVALID] source={} (ok={}) -> target={} "
              "(ok={})",
              dep.get(), source_ok, pass.get(), target_ok);
            ++dumped;
          }
        }
      }
    }
    if (edge_count > 0) {
      LOG_F(5,
        "[RenderGraph][Deps][Summary] total_edges={} valid_edges={} "
        "invalid_edges={} missing_sources={} missing_targets={}",
        edge_count, valid_edges, invalid_edges, missing_sources,
        missing_targets);
    } else {
      LOG_F(
        5, "[RenderGraph][Deps][Summary] no explicit dependencies recorded");
    }

    auto classify_bucket = [](int d) {
      if (d <= 0)
        return 0; // bucket 0
      if (d == 1)
        return 1; // bucket 1
      if (d == 2)
        return 2; // bucket 2
      return 3; // bucket 3+ consolidated
    };

    int indeg_buckets[4] { 0, 0, 0, 0 };
    int outdeg_buckets[4] { 0, 0, 0, 0 };
    for (auto h : order) {
      indeg_buckets[classify_bucket(remaining_deps[h])]++;
      outdeg_buckets[classify_bucket(out_degree[h])]++;
    }

    // Check for linear chain pattern (all passes have indegree/outdegree <=1,
    // edges = n-1)
    bool linear_chain = (order.size() > 1 && edge_count == order.size() - 1);
    if (linear_chain) {
      int starts = 0, ends = 0;
      for (auto h : order) {
        if (remaining_deps[h] > 1 || out_degree[h] > 1) {
          linear_chain = false;
          break;
        }
        if (remaining_deps[h] == 0)
          ++starts;
        if (out_degree[h] == 0)
          ++ends;
      }
      if (!(starts == 1 && ends == 1))
        linear_chain = false;
    }

    LOG_F(5,
      "[RenderGraph][Diag] passes={} edges={} indegree{{0/1/2/3+}}={}/{}/{}/{} "
      "outdegree{{0/1/2/3+}}={}/{}/{}/{} linear_chain={}",
      order.size(), edge_count, indeg_buckets[0], indeg_buckets[1],
      indeg_buckets[2], indeg_buckets[3], outdeg_buckets[0], outdeg_buckets[1],
      outdeg_buckets[2], outdeg_buckets[3], linear_chain);
  }

  std::vector<std::vector<PassHandle>> batches;
  std::unordered_set<PassHandle> scheduled;
  while (scheduled.size() < order.size()) {
    std::vector<PassHandle> batch;
    for (auto h : order) {
      if (scheduled.count(h))
        continue;
      if (remaining_deps[h] == 0)
        batch.push_back(h);
    }
    if (batch.empty()) {
      LOG_F(ERROR, "[RenderGraph] Deadlock building batches (cycle?)");
      break;
    }
    // Mark scheduled and decrement dependents
    for (auto h : batch)
      scheduled.insert(h);
    // For each pass scheduled now, decrement remaining_deps of passes that
    // depend on it
    for (const auto& [pass, deps] : explicit_dependencies_) {
      for (auto dep : deps) {
        if (std::find(batch.begin(), batch.end(), dep) != batch.end()) {
          auto it = remaining_deps.find(pass);
          if (it != remaining_deps.end())
            --it->second;
        }
      }
    }
    batches.push_back(std::move(batch));
  }

  LOG_F(3, "[RenderGraph] Built {} execution batches", batches.size());

  if (loguru::g_stderr_verbosity >= 5) {
    size_t max_width = 0;
    size_t total_width = 0;
    for (const auto& b : batches) {
      max_width = std::max(max_width, b.size());
      total_width += b.size();
    }
    const size_t total_passes = order.size();
    const double avg_width = batches.empty()
      ? 0.0
      : static_cast<double>(total_width) / static_cast<double>(batches.size());
    // Simple parallelization potential heuristic: fraction of passes that
    // shared a batch with at least one sibling
    size_t shared_count = 0;
    for (const auto& b : batches) {
      if (b.size() > 1)
        shared_count += b.size();
    }
    double parallelization_potential = total_passes > 0
      ? static_cast<double>(shared_count) / static_cast<double>(total_passes)
      : 0.0;
    LOG_F(5,
      "[RenderGraph][Diag] batches={} max_width={} avg_width={:.2f} "
      "parallel_pass_fraction={:.2f} serialization_ratio={:.2f}",
      batches.size(), max_width, avg_width, parallelization_potential,
      (total_passes > 0 ? static_cast<double>(batches.size())
            / static_cast<double>(total_passes)
                        : 0.0));
    if (max_width <= 1 && total_passes > 1) {
      LOG_F(5,
        "[RenderGraph][Diag] All batches width=1 -> full serialization. "
        "Causes: dependency chain or overly conservative dependency "
        "recording.");
    }
  }

  const bool multiview = IsMultiViewEnabled() && !frame_context_.views.empty();
  TaskExecutionContext
    exec_ctx; // Serial context for non-parallel or main-thread-only passes
  for (size_t bi = 0; bi < batches.size(); ++bi) {
    auto& batch = batches[bi];
    bool want_parallel = IsParallelBatchExecutionEnabled();
    bool can_parallel = want_parallel && batch.size() > 1;
    LOG_F(4, "[RenderGraph] Executing batch {} ({} passes){}", bi, batch.size(),
      can_parallel ? " [parallel]" : " [serial]");
    if (loguru::g_stderr_verbosity >= 5) {
      if (!want_parallel) {
        LOG_F(5,
          "[RenderGraph][Batch{}] forcing serial: global parallel disabled",
          bi);
      } else if (batch.size() <= 1) {
        LOG_F(5,
          "[RenderGraph][Batch{}] serial: width=1 (no concurrency opportunity)",
          bi);
      }
    }

    const auto batch_start = std::chrono::high_resolution_clock::now();

    if (!can_parallel) {
      // Fallback serial path
      for (auto handle : batch) {
        if (auto* pass = GetPassMutable(handle)) {
          auto exec_one = [&](uint32_t view_index) {
            pass->SetViewIndex(view_index);
            auto start = std::chrono::high_resolution_clock::now();
            if (pass_cost_profiler_)
              pass_cost_profiler_->BeginPass(handle);
            pass->Execute(exec_ctx);
            auto end = std::chrono::high_resolution_clock::now();
            auto cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(
              end - start)
                            .count();
            if (pass_cost_profiler_) {
              pass_cost_profiler_->RecordCpuTime(
                handle, static_cast<float>(cpu_us));
              pass_cost_profiler_->RecordGpuTime(
                handle, static_cast<float>(cpu_us));
              pass_cost_profiler_->EndPass(handle);
            }
          };
          if (multiview && pass->GetScope() == PassScope::PerView) {
            for (uint32_t vi = 0; vi < frame_context_.views.size(); ++vi) {
              exec_one(vi);
            }
          } else {
            exec_one(pass->GetViewIndex());
          }
        }
      }
    } else {
      // Parallel path: dispatch each pass (and each view instance if per-view)
      // as a separate task.
      struct TaskRecord {
        PassHandle handle;
        uint32_t view_index;
      };
      // Timing storage (must exist before possible main-thread execution path
      // uses it). Use a vector indexed by the underlying PassHandle id to
      // avoid concurrent unordered_map insertions (which triggered ASan UAF
      // due to node allocations during concurrent access).
      struct Timing {
        std::atomic<long long> total_cpu_us { 0 };
      };
      uint32_t max_pass_id = 0;
      for (auto h : batch) {
        max_pass_id = std::max<uint32_t>(max_pass_id, h.get());
      }
      // Reserve space for multiview expansion worst case (optional; ids are
      // stable so capacity only needs max id + 1).
      std::unique_ptr<Timing[]> timing_vec {
        new Timing[std::max<uint32_t>(1, max_pass_id + 1)]
      };
      std::vector<TaskRecord> tasks;
      tasks.reserve(
        batch.size() * (multiview ? frame_context_.views.size() : 1));
      for (auto handle : batch) {
        if (auto* pass = GetPassMutable(handle)) {
          if (pass->RequiresMainThread()) {
            // Execute immediately on main thread (respect view iteration)
            auto exec_main = [&](uint32_t view_index) {
              pass->SetViewIndex(view_index);
              auto start = std::chrono::high_resolution_clock::now();
              if (pass_cost_profiler_)
                pass_cost_profiler_->BeginPass(handle);
              // Main-thread path uses shared exec_ctx serially
              pass->Execute(exec_ctx);
              auto end = std::chrono::high_resolution_clock::now();
              auto cpu_us
                = std::chrono::duration_cast<std::chrono::microseconds>(
                  end - start)
                    .count();
              if (pass_cost_profiler_) {
                pass_cost_profiler_->RecordCpuTime(
                  handle, static_cast<float>(cpu_us));
                pass_cost_profiler_->RecordGpuTime(
                  handle, static_cast<float>(cpu_us));
                pass_cost_profiler_->EndPass(handle);
              }
              timing_vec[handle.get()].total_cpu_us.fetch_add(
                cpu_us, std::memory_order_relaxed);
            };
            if (multiview && pass->GetScope() == PassScope::PerView) {
              for (uint32_t vi = 0; vi < frame_context_.views.size(); ++vi)
                exec_main(vi);
            } else {
              exec_main(pass->GetViewIndex());
            }
            continue; // skip adding job
          }
          if (multiview && pass->GetScope() == PassScope::PerView) {
            for (uint32_t vi = 0; vi < frame_context_.views.size(); ++vi) {
              tasks.push_back({ handle, vi });
            }
          } else {
            tasks.push_back({ handle, pass->GetViewIndex() });
          }
        }
      }
      // No dynamic reservations needed for timing_vec (fixed size by max id).

      std::vector<co::Co<>> jobs;
      jobs.reserve(tasks.size());
      for (auto& t : tasks) {
        if (!GetPassMutable(t.handle))
          continue;
        jobs.push_back([&, h = t.handle,
                         view_index = t.view_index]() -> co::Co<> {
          co_await context.GetThreadPool().Run([&, h, view_index](
                                                 co::ThreadPool::CancelToken) {
            auto* p = GetPassMutable(h);
            if (!p)
              return;
            p->SetViewIndex(view_index);
            // Create a fresh execution context per task to avoid concurrent
            // access to shared state (e.g., CommandRecorder unique_ptr)
            TaskExecutionContext local_ctx;
            // Propagate view context (if available) - multiview ensures
            // frame_context_.views is valid.
            if (view_index < frame_context_.views.size()) {
              local_ctx.SetViewContext(frame_context_.views[view_index]);
            }
            // Mark as parallel safe for passes wanting to branch behavior
            local_ctx.SetParallelSafe(true);
            auto start = std::chrono::high_resolution_clock::now();
            if (pass_cost_profiler_)
              pass_cost_profiler_->BeginPass(h);
            p->Execute(local_ctx);
            auto end = std::chrono::high_resolution_clock::now();
            auto cpu_us = std::chrono::duration_cast<std::chrono::microseconds>(
              end - start)
                            .count();
            if (pass_cost_profiler_) {
              pass_cost_profiler_->RecordCpuTime(h, static_cast<float>(cpu_us));
              pass_cost_profiler_->RecordGpuTime(h, static_cast<float>(cpu_us));
              pass_cost_profiler_->EndPass(h);
            }
            timing_vec[h.get()].total_cpu_us.fetch_add(
              cpu_us, std::memory_order_relaxed);
          });
        }());
      }

      // Use OxCo AllOf helper to await all tasks concurrently
      co_await co::AllOf(std::move(jobs));

      // Batch timing summary
      long long sum_cpu_us = 0;
      if (timing_vec) {
        for (uint32_t pid = 0; pid <= max_pass_id; ++pid) {
          sum_cpu_us
            += timing_vec[pid].total_cpu_us.load(std::memory_order_relaxed);
        }
      }
      auto batch_end = std::chrono::high_resolution_clock::now();
      auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
        batch_end - batch_start)
                       .count();
      if (wall_us > 0 && sum_cpu_us > 0) {
        const double speedup
          = static_cast<double>(sum_cpu_us) / static_cast<double>(wall_us);
        LOG_F(4,
          "[RenderGraph][Parallel] batch={} tasks={} wall={}us sum_cpu={}us "
          "speedup_x={:.2f}",
          bi, tasks.size(), wall_us, sum_cpu_us, speedup);
      }
    }
  }
  LOG_F(3, "[RenderGraph] ExecutePassBatches complete");
  co_return;
}

auto AsyncEngineRenderGraph::PresentResults(ModuleContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] PresentResults");
  LOG_F(
    3, "[RenderGraph] PresentResults ({} views)", frame_context_.views.size());
  // Schedule reclaim for frame-local resources now that frame execution is
  // done. Future: Use lifetime analysis to reclaim earlier when last usage pass
  // completes.
  size_t reclaimed_candidates = 0;
  for (const auto& [handle, desc] : resource_descriptors_) {
    if (desc && desc->GetLifetime() == ResourceLifetime::FrameLocal
      && desc->HasGraphicsIntegration()) {
      if (auto* gi = desc->GetGraphicsIntegration()) {
        gi->ScheduleResourceReclaim(RenderGraphResourceHandle { handle.get() },
          frame_context_.frame_index, desc->GetDebugName());
        ++reclaimed_candidates;
      }
    }
  }
  LOG_F(3, "[RenderGraph] Scheduled {} frame-local resources for reclaim",
    reclaimed_candidates);
  // Process any completed frames (simulation step)
  // NOTE: In a real engine this would be driven by GPU fence completion.
  co_return;
}

auto CreateAsyncEngineRenderGraph() -> std::unique_ptr<RenderGraph>
{
  return std::make_unique<AsyncEngineRenderGraph>();
}

} // namespace oxygen::examples::asyncsim
