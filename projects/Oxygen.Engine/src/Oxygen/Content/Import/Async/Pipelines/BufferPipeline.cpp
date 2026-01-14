//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/util/Signature.h>

namespace oxygen::content::import {

BufferPipeline::BufferPipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

BufferPipeline::~BufferPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "BufferPipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto BufferPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "BufferPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto BufferPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  co_await input_channel_.Send(std::move(item));
}

auto BufferPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed()) {
    return false;
  }

  if (input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
  }
  return ok;
}

auto BufferPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .cooked = {},
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  co_return std::move(*maybe_result);
}

auto BufferPipeline::Close() -> void { input_channel_.Close(); }

auto BufferPipeline::CancelAll() -> void
{
  input_channel_.Close();
  output_channel_.Close();
}

auto BufferPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto BufferPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto BufferPipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);

    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    DLOG_F(1, "processing {}", item.source_id);

    std::vector<ImportDiagnostic> diagnostics;
    if (auto diag = co_await ComputeContentHash(item); diag.has_value()) {
      diagnostics.push_back(std::move(*diag));
    }

    WorkResult result {
      .source_id = std::move(item.source_id),
      .cooked = std::move(item.cooked),
      .diagnostics = std::move(diagnostics),
      .success = diagnostics.empty(),
    };

    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto BufferPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult cancelled {
    .source_id = std::move(item.source_id),
    .cooked = std::move(item.cooked),
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(cancelled));
}

auto BufferPipeline::ComputeContentHash(WorkItem& item)
  -> co::Co<std::optional<ImportDiagnostic>>
{
  if (!config_.with_content_hashing) {
    co_return std::nullopt;
  }

  // Already computed - skip and no diagnostic
  if (item.cooked.content_hash != 0) {
    co_return std::nullopt;
  }

  std::span<const std::byte> bytes(
    item.cooked.data.data(), item.cooked.data.size());

  const auto content_hash
    = co_await thread_pool_.Run([bytes, &item]() noexcept {
        const auto hash = util::ComputeContentHash(bytes);
        DLOG_F(2, "hashed {} -> {:#x}", item.source_id, hash);
        return hash;
      });
  item.cooked.content_hash = content_hash;
  co_return std::nullopt;
}

} // namespace oxygen::content::import
