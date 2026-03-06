//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto IsStopRequested(const std::stop_token& token) noexcept
    -> bool
  {
    return token.stop_possible() && token.stop_requested();
  }

  auto PatchContentHash(
    std::vector<std::byte>& bytes, const uint64_t content_hash) -> void
  {
    constexpr auto kOffset
      = offsetof(data::pak::physics::CollisionShapeAssetDesc, header)
      + offsetof(data::pak::core::AssetHeader, content_hash);
    if (bytes.size() < kOffset + sizeof(content_hash)) {
      return;
    }
    std::memcpy(bytes.data() + kOffset, &content_hash, sizeof(content_hash));
  }

  [[nodiscard]] auto SerializeDescriptor(
    data::pak::physics::CollisionShapeAssetDesc descriptor,
    const std::vector<data::pak::physics::CompoundShapeChildDesc>& children)
    -> std::vector<std::byte>
  {
    using data::pak::physics::ShapeType;
    if (descriptor.shape_type == ShapeType::kCompound) {
      descriptor.shape_params.compound.child_count
        = static_cast<uint32_t>(children.size());
      descriptor.shape_params.compound.child_byte_offset
        = children.empty() ? 0U : static_cast<uint32_t>(sizeof(descriptor));
    }

    auto stream = serio::MemoryStream {};
    auto writer = serio::Writer(stream);
    [[maybe_unused]] const auto pack = writer.ScopedAlignment(1);
    [[maybe_unused]] const auto write = writer.WriteBlob(std::as_bytes(
      std::span<const data::pak::physics::CollisionShapeAssetDesc, 1>(
        &descriptor, 1)));
    DCHECK_F(
      write.has_value(), "Collision shape descriptor serialization failed");
    if (descriptor.shape_type == ShapeType::kCompound && !children.empty()) {
      [[maybe_unused]] const auto child_write
        = writer.WriteBlob(std::as_bytes(std::span(children)));
      DCHECK_F(child_write.has_value(),
        "Collision shape compound children serialization failed");
    }
    const auto bytes = stream.Data();
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  }

} // namespace

CollisionShapeImportPipeline::CollisionShapeImportPipeline(
  co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config_.queue_capacity)
  , output_channel_(config_.queue_capacity)
{
}

CollisionShapeImportPipeline::~CollisionShapeImportPipeline()
{
  if (started_) {
    DLOG_IF_F(
      WARNING, HasPending(), "Destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto CollisionShapeImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(
    !started_, "CollisionShapeImportPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto CollisionShapeImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto CollisionShapeImportPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto CollisionShapeImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .descriptor_bytes = {},
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto CollisionShapeImportPipeline::Close() -> void { input_channel_.Close(); }

auto CollisionShapeImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto CollisionShapeImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto CollisionShapeImportPipeline::GetProgress() const noexcept
  -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto CollisionShapeImportPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (IsStopRequested(item.stop_token)) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    if (item.on_started) {
      item.on_started();
    }

    const auto cook_start = std::chrono::steady_clock::now();
    auto descriptor_bytes
      = SerializeDescriptor(item.descriptor, item.compound_children);
    if (config_.with_content_hashing) {
      const auto content_hash
        = co_await ComputeContentHash(descriptor_bytes, item.stop_token);
      if (!content_hash.has_value()) {
        co_await ReportCancelled(std::move(item));
        continue;
      }
      PatchContentHash(descriptor_bytes, *content_hash);
    }

    auto result = WorkResult {
      .source_id = std::move(item.source_id),
      .descriptor_bytes = std::move(descriptor_bytes),
      .diagnostics = {},
      .telemetry = ImportWorkItemTelemetry {
        .cook_duration = MakeDuration(
          cook_start, std::chrono::steady_clock::now()),
      },
      .success = true,
    };
    if (item.on_finished) {
      item.on_finished();
    }
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto CollisionShapeImportPipeline::ComputeContentHash(
  std::vector<std::byte>& descriptor_bytes, const std::stop_token stop_token)
  -> co::Co<std::optional<uint64_t>>
{
  if (IsStopRequested(stop_token)) {
    co_return std::nullopt;
  }

  const auto bytes = std::span<const std::byte>(
    descriptor_bytes.data(), descriptor_bytes.size());
  const auto content_hash = co_await thread_pool_.Run(
    [bytes, stop_token](co::ThreadPool::CancelToken canceled) noexcept {
      if (IsStopRequested(stop_token) || canceled) {
        return uint64_t { 0 };
      }
      return util::ComputeContentHash(bytes);
    });

  if (IsStopRequested(stop_token) || content_hash == 0) {
    co_return std::nullopt;
  }
  co_return content_hash;
}

auto CollisionShapeImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  auto canceled = WorkResult {
    .source_id = std::move(item.source_id),
    .descriptor_bytes = {},
    .diagnostics = {},
    .success = false,
  };
  if (item.on_finished) {
    item.on_finished();
  }
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
