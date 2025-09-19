//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraph.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <deque>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/ThreadPool.h>

#include "../Integration/GraphicsLayerIntegration.h"
#include "ExecutionContext.h"
#include "Resource.h"
#include "Scheduler.h"
#include "Validator.h"
#include <Oxygen/Core/FrameContext.h>

namespace oxygen::engine::asyncsim {

// C++20 constexpr constants for better maintainability
namespace {
  constexpr size_t kMaxDependencyDumpEntries = 32;
  constexpr int kDependencyBucketThreshold3Plus = 3;
  constexpr size_t kMinBatchSizeForParallel = 2;
  constexpr int kDependencyBucketCount = 4;
} // anonymous namespace

// Internal helpers (local TU scope)
namespace {
  // Iterate over required invocations for a pass based on its scope & views.
  template <class F>
  inline void ForEachInvocation(
    RenderPass& pass, std::span<const ViewInfo> views, F&& f)
  {
    switch (pass.GetScope()) {
    case PassScope::PerView:
      if (!views.empty()) {
        for (uint32_t vi = 0u; vi < views.size(); ++vi) {
          f(ViewIndex { vi });
        }
      }
      break; // Skip if no views
    case PassScope::Shared:
    case PassScope::Viewless:
      f(pass.GetViewIndex());
      break;
    }
  }

  class PassProfileScope {
  public:
    PassProfileScope(PassCostProfiler* profiler, PassHandle h) noexcept
      : profiler_(profiler)
      , handle_(h)
    {
      if (profiler_)
        profiler_->BeginPass(handle_);
    }
    ~PassProfileScope()
    {
      if (profiler_)
        profiler_->EndPass(handle_);
    }
    void record_times(float cpu_us, float gpu_us) const noexcept
    {
      if (!profiler_)
        return;
      profiler_->RecordCpuTime(handle_, cpu_us);
      profiler_->RecordGpuTime(handle_, gpu_us);
    }

  private:
    PassCostProfiler* profiler_ { nullptr };
    PassHandle handle_ { 0 };
  };
} // namespace

//! Pass execution batch for parallelism
struct PassBatch {
  std::vector<PassHandle> passes;
  QueueType queue_type;
  bool can_execute_parallel;
  std::vector<ViewIndex> view_indices; // Views this batch applies to

  PassBatch(QueueType queue = QueueType::Graphics)
    : queue_type(queue)
    , can_execute_parallel(false)
  {
  }
};

// The AsyncRenderGraph implementations below provide the concrete async
// coroutine-driven execution pipeline. We expose Impl methods invoked by the
// Default adapters constructed in the AsyncRenderGraph ctor.

// AsyncRenderGraph constructor: create adapters bound to this and forward
// ownership (unique_ptr) to base RenderGraph via placement of adapters.
AsyncRenderGraph::AsyncRenderGraph()
  : RenderGraph(std::make_unique<struct ExecAdapter>(this),
      std::make_unique<struct SchedulerAdapter>(this),
      std::make_unique<struct PlannerAdapter>(this))
{
}

auto AsyncRenderGraph::ExecuteImpl(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] Async execute");
  LOG_F(2, "{} passes", passes_.size());
  co_await PlanResourceTransitions(context);
  co_await ExecutePassBatches(context);
  co_await PresentResults(context);
  co_return;
}

auto AsyncRenderGraph::PlanResourceTransitionsImpl(FrameContext& context)
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
    resource_state_tracker_.SetInitialState(
      handle, ResourceState::Undefined, ViewIndex { 0 });
  }

  const auto& views = frame_context_.GetViews();
  const auto& order
    = execution_order_.empty() ? GetPassHandles() : execution_order_;
  for (auto handle : order) {
    auto* pass = GetPassMutable(handle);
    if (!pass)
      continue;
    const auto view_index = pass->GetViewIndex();
    const auto& reads = pass->GetReadResources();
    const auto& read_states = pass->GetReadStates();
    for (const auto& [resource, state] : std::views::zip(reads, read_states)) {
      resource_state_tracker_.RequestTransition(
        resource, state, handle, view_index);
    }
    const auto& writes = pass->GetWriteResources();
    const auto& write_states = pass->GetWriteStates();
    for (const auto& [resource, state] :
      std::views::zip(writes, write_states)) {
      resource_state_tracker_.RequestTransition(
        resource, state, handle, view_index);
    }

    // Handle per-view resource transitions
    if (pass->GetScope() == PassScope::PerView && !views.empty()) {
      for (ViewIndex vi = ViewIndex { 1 }; vi.get() < views.size();
        (void)++vi) {
        for (const auto& [resource, state] :
          std::views::zip(reads, read_states)) {
          resource_state_tracker_.RequestTransition(
            resource, state, handle, vi);
        }
        for (const auto& [resource, state] :
          std::views::zip(writes, write_states)) {
          resource_state_tracker_.RequestTransition(
            resource, state, handle, vi);
        }
      }
    }
  }

  const auto& planned = resource_state_tracker_.GetPlannedTransitions();
  LOG_F(3, "[RenderGraph] Planned {} resource transitions", planned.size());
  co_return;
}

auto AsyncRenderGraph::ExecutePassBatchesImpl(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] ExecutePassBatches");
  LOG_F(3, "[RenderGraph] ExecutePassBatches begin");

  const auto& order
    = execution_order_.empty() ? GetPassHandles() : execution_order_;
  auto remaining_deps = BuildDependencyGraph(order);
  LogDependencyDiagnostics(order, remaining_deps);
  auto batches = BuildExecutionBatches(order, remaining_deps);

  LOG_F(3, "[RenderGraph] Built {} execution batches", batches.size());

  const auto& views = frame_context_.GetViews();
  TaskExecutionContext exec_ctx;

  for (size_t bi = 0; bi < batches.size(); ++bi) {
    auto& batch = batches[bi];
    bool want_parallel = IsParallelBatchExecutionEnabled();
    bool can_parallel = want_parallel && batch.size() > 1;
    if (can_parallel && !context.GetThreadPool()) {
      can_parallel = false;
      LOG_F(5, "[RenderGraph][Batch%zu] forcing serial: no thread pool", bi);
    }
    for (auto h : batch) {
      if (!GetPassMutable(h)) {
        LOG_F(ERROR, "[RenderGraph] Missing pass for handle {} in batch {}",
          h.get(), bi);
        can_parallel = false; // force serial safe path
      }
    }

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

    if (!can_parallel) {
      ExecuteBatchSerial(batch, views, exec_ctx);
    } else {
      auto batch_start = std::chrono::high_resolution_clock::now();
      co_await ExecuteBatchParallel(
        context, batch, bi, views, exec_ctx, batch_start);
    }
  }

  LOG_F(3, "[RenderGraph] ExecutePassBatches complete");
  co_return;
}

auto AsyncRenderGraph::BuildDependencyGraph(
  const std::vector<PassHandle>& order) -> std::unordered_map<PassHandle, int>
{
  // Build batches (level sets) from dependency graph in scheduling result.
  // We only have explicit dependencies stored; create inverse indegree counts
  // using execution order for determinism.
  std::unordered_map<PassHandle, int> remaining_deps;
  for (auto h : order)
    remaining_deps[h] = 0;
  for (const auto& [pass, deps] : explicit_dependencies_) {
    auto it = remaining_deps.find(pass);
    if (it == remaining_deps.end())
      continue;
    it->second += static_cast<int>(deps.size());
  }
  return remaining_deps;
}

auto AsyncRenderGraph::LogDependencyDiagnostics(
  const std::vector<PassHandle>& order,
  const std::unordered_map<PassHandle, int>& remaining_deps) -> void
{
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
    const size_t max_dump = kMaxDependencyDumpEntries; // avoid log spam
    size_t dumped = 0;
    auto in_order
      = [&](PassHandle h) { return std::ranges::contains(order, h); };
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
      return kDependencyBucketThreshold3Plus; // bucket 3+ consolidated
    };

    std::array<int, kDependencyBucketCount> indeg_buckets {};
    std::array<int, kDependencyBucketCount> outdeg_buckets {};
    for (auto h : order) {
      indeg_buckets[classify_bucket(remaining_deps.at(h))]++;
      outdeg_buckets[classify_bucket(out_degree[h])]++;
    }

    // Check for linear chain pattern (all passes have indegree/outdegree <=1,
    // edges = n-1)
    bool linear_chain = (order.size() > 1 && edge_count == order.size() - 1);
    if (linear_chain) {
      int starts = 0, ends = 0;
      for (auto h : order) {
        if (remaining_deps.at(h) > 1 || out_degree[h] > 1) {
          linear_chain = false;
          break;
        }
        if (remaining_deps.at(h) == 0)
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
}

auto AsyncRenderGraph::BuildExecutionBatches(
  const std::vector<PassHandle>& order,
  std::unordered_map<PassHandle, int>& remaining_deps)
  -> std::vector<std::vector<PassHandle>>
{
  // Build adjacency (dependents) list once for O(V+E)
  std::unordered_map<PassHandle, std::vector<PassHandle>> dependents;
  dependents.reserve(explicit_dependencies_.size());
  for (const auto& [pass, deps] : explicit_dependencies_) {
    for (auto dep : deps) {
      dependents[dep].push_back(pass); // dep -> pass
    }
  }

  std::deque<PassHandle> ready;
  for (auto h : order) {
    if (remaining_deps[h] == 0)
      ready.push_back(h);
  }

  std::vector<std::vector<PassHandle>> batches;
  batches.reserve(order.size());
  size_t scheduled_count = 0;
  while (!ready.empty()) {
    // Deterministic ordering: we collect all currently ready nodes whose
    // indegree is zero now.
    std::vector<PassHandle> batch;
    batch.reserve(ready.size());
    // Drain current level
    size_t level_size = ready.size();
    for (size_t i = 0; i < level_size; ++i) {
      auto h = ready.front();
      ready.pop_front();
      batch.push_back(h);
    }
    // Process their outgoing edges
    for (auto h : batch) {
      ++scheduled_count;
      if (auto it = dependents.find(h); it != dependents.end()) {
        for (auto dep_target : it->second) {
          auto it_in = remaining_deps.find(dep_target);
          if (it_in != remaining_deps.end()) {
            int new_val = --it_in->second;
            if (new_val == 0) {
              ready.push_back(dep_target);
            }
          }
        }
      }
    }
    batches.push_back(std::move(batch));
  }

  if (scheduled_count != order.size()) {
    // Cycle detected: collect remaining nodes with indegree > 0
    std::vector<PassHandle> stuck;
    stuck.reserve(order.size() - scheduled_count);
    for (auto h : order) {
      if (remaining_deps[h] > 0)
        stuck.push_back(h);
    }
    LOG_F(ERROR,
      "[RenderGraph] Cycle detected in render graph ({} nodes stuck)",
      stuck.size());
    size_t dump = 0;
    for (auto h : stuck) {
      if (dump++ >= 8)
        break; // limit
      LOG_F(ERROR, "  stuck pass handle={} indegree={} name='{}'", h.get(),
        remaining_deps[h],
        (GetPassMutable(h) ? GetPassMutable(h)->GetDebugName().c_str()
                           : "<missing>"));
    }
  }

  LOG_F(3, "[RenderGraph] Built {} execution batches", batches.size());
  // Compute batch metrics and store
  {
    size_t max_width = 0;
    size_t total_width = 0;
    size_t shared_count = 0;
    for (const auto& b : batches) {
      max_width = std::max(max_width, b.size());
      total_width += b.size();
      if (b.size() > 1)
        shared_count += b.size();
    }
    const size_t total_passes = order.size();
    batch_metrics_.batch_count = static_cast<uint32_t>(batches.size());
    batch_metrics_.max_width = static_cast<uint32_t>(max_width);
    batch_metrics_.avg_width = batches.empty()
      ? 0.0
      : static_cast<double>(total_width) / static_cast<double>(batches.size());
    batch_metrics_.parallel_pass_fraction = total_passes
      ? static_cast<double>(shared_count) / static_cast<double>(total_passes)
      : 0.0;
  }

  if (loguru::g_stderr_verbosity >= 4) {
    LOG_F(4,
      "[RenderGraph][Batches] count={} max_width={} avg_width={:.2f} "
      "parallel_fraction={:.2f}",
      batch_metrics_.batch_count, batch_metrics_.max_width,
      batch_metrics_.avg_width, batch_metrics_.parallel_pass_fraction);
    if (batch_metrics_.max_width <= 1 && order.size() > 1) {
      LOG_F(4,
        "[RenderGraph][Batches] Full serialization detected (consider "
        "dependency review)");
    }
  }

  return batches;
}

auto AsyncRenderGraph::ExecuteBatchSerial(const std::vector<PassHandle>& batch,
  const std::span<const ViewInfo>& views, TaskExecutionContext& exec_ctx)
  -> void
{
  using clock = std::chrono::steady_clock;
  // Local helper to execute a single pass invocation set (all needed view
  // indices)
  auto execute_pass_invocations = [&](RenderPass& pass, PassHandle handle) {
    ForEachInvocation(pass, views, [&](ViewIndex view_index) {
      pass.SetViewIndex(
        view_index); // TODO: remove mutation by making Execute view-index param
      PassProfileScope prof(pass_cost_profiler_.get(), handle);
      const auto start = clock::now();
      pass.Execute(exec_ctx);
      const auto end = clock::now();
      const auto cpu_us
        = std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count();
      prof.record_times(static_cast<float>(cpu_us), static_cast<float>(cpu_us));
    });
  };
  for (auto handle : batch) {
    auto* pass = GetPassMutable(handle);
    if (!pass)
      continue;
    execute_pass_invocations(*pass, handle);
  }
}

auto AsyncRenderGraph::ExecuteBatchParallel(FrameContext& context,
  const std::vector<PassHandle>& batch, size_t bi,
  const std::span<const ViewInfo>& views, TaskExecutionContext& exec_ctx,
  const std::chrono::high_resolution_clock::time_point& batch_start) -> co::Co<>
{
  using clock = std::chrono::steady_clock;
  struct Timing {
    std::atomic<long long> cpu_us { 0 };
  };
  // One timing slot per pass in batch (dense)
  std::vector<Timing> timings(batch.size());

  std::vector<co::Co<>> jobs;
  jobs.reserve(batch.size());
  for (size_t idx = 0; idx < batch.size(); ++idx) {
    PassHandle handle = batch[idx];
    auto* pass = GetPassMutable(handle);
    if (!pass)
      continue;

    if (pass->RequiresMainThread()) {
      // Execute synchronously on main thread (serial inside parallel batch)
      ForEachInvocation(*pass, views, [&](ViewIndex view_index) {
        pass->SetViewIndex(view_index); // TODO remove mutation later
        PassProfileScope prof(pass_cost_profiler_.get(), handle);
        const auto start = clock::now();
        pass->Execute(exec_ctx); // shared exec_ctx acceptable (main thread)
        const auto end = clock::now();
        const auto cpu
          = std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count();
        prof.record_times(static_cast<float>(cpu), static_cast<float>(cpu));
        timings[idx].cpu_us.fetch_add(cpu, std::memory_order_relaxed);
      });
      continue;
    }

    jobs.push_back([&, handle, idx]() -> co::Co<> {
      co_await context.GetThreadPool()->Run([&, handle, idx](
                                              co::ThreadPool::CancelToken) {
        auto* p = GetPassMutable(handle);
        if (!p)
          return;
        ForEachInvocation(*p, views, [&](ViewIndex view_index) {
          p->SetViewIndex(view_index); // shared pass object, but no concurrent
                                       // mutation hazard currently
          TaskExecutionContext local_ctx;
          if (view_index.get() < views.size()) {
            local_ctx.SetViewInfo(views[view_index.get()]);
          }
          local_ctx.SetParallelSafe(true);
          PassProfileScope prof(pass_cost_profiler_.get(), handle);
          const auto start = clock::now();
          p->Execute(local_ctx);
          const auto end = clock::now();
          const auto cpu
            = std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();
          prof.record_times(static_cast<float>(cpu), static_cast<float>(cpu));
          timings[idx].cpu_us.fetch_add(cpu, std::memory_order_relaxed);
        });
      });
    }());
  }

  if (!jobs.empty()) {
    co_await co::AllOf(std::move(jobs));
  }

  long long sum_cpu_us = 0;
  for (auto& t : timings)
    sum_cpu_us += t.cpu_us.load(std::memory_order_relaxed);
  auto batch_end = clock::now();
  auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
    batch_end - batch_start)
                   .count();
  if (wall_us > 0 && sum_cpu_us > 0) {
    double speedup
      = static_cast<double>(sum_cpu_us) / static_cast<double>(wall_us);
    LOG_F(4,
      "[RenderGraph][Parallel] batch={} tasks={} wall={}us sum_cpu={}us "
      "speedup_x={:.2f}",
      bi, batch.size(), wall_us, sum_cpu_us, speedup);
  }
}

auto AsyncRenderGraph::PresentResultsImpl(FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "[RenderGraph] PresentResults");
  LOG_F(3, "[RenderGraph] PresentResults ({} views)",
    frame_context_.GetViews().size());
  // Schedule reclaim for frame-local resources now that frame execution is
  // done. Future: Use lifetime analysis to reclaim earlier when last usage pass
  // completes.
  size_t reclaimed_candidates = 0;
  for (const auto& [handle, desc] : resource_descriptors_) {
    if (desc && desc->GetLifetime() == ResourceLifetime::FrameLocal) {
      if (auto gfx = context.AcquireGraphics()) {
        gfx->ScheduleResourceReclaim(ResourceHandle { handle.get() },
          frame_context_.GetFrameIndex(), desc->GetDebugName());
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

auto CreateAsyncRenderGraph() -> std::unique_ptr<RenderGraph>
{
  return std::make_unique<AsyncRenderGraph>();
}

} // namespace oxygen::engine::asyncsim
