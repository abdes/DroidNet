//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto IsStopRequested(const std::stop_token& token) noexcept
    -> bool
  {
    return token.stop_possible() && token.stop_requested();
  }

  [[nodiscard]] auto SerializeDescriptor(
    data::pak::physics::CollisionShapeAssetDesc descriptor,
    const std::string_view name,
    const std::vector<data::pak::physics::CompoundShapeChildDesc>& children)
    -> std::vector<std::byte>
  {
    using data::pak::physics::ShapeType;
    auto strings = data::DescriptorStringsBuilder {};
    descriptor.header.name_offset = strings.Add(name);
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
    const auto data = stream.Data();
    auto bytes = std::vector<std::byte> { data.begin(), data.end() };
    data::AppendDescriptorStrings(bytes, strings);
    return bytes;
  }

} // namespace

CollisionShapeImportPipeline::CollisionShapeImportPipeline()
  : CollisionShapeImportPipeline(Config {})
{
}

CollisionShapeImportPipeline::CollisionShapeImportPipeline(Config config)
  : config_(config)
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
    nursery.Start([this] -> co::Co<> { co_await Worker(); });
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
      = SerializeDescriptor(item.descriptor, item.name, item.compound_children);

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
