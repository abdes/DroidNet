//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <functional>
#include <queue>
#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/ImportPlanner.h>

namespace oxygen::content::import {

[[nodiscard]] auto ReadinessTracker::IsReady() const noexcept -> bool
{
  if (ready_event == nullptr) {
    return false;
  }

  return ready_event->ready;
}

auto ReadinessTracker::MarkReady(const DependencyToken& token) -> bool
{
  if (ready_event == nullptr) {
    return false;
  }

  auto matched = false;
  for (size_t index = 0; index < required.size(); ++index) {
    if (required[index] != token.producer) {
      continue;
    }

    matched = true;
    if (satisfied[index] == 0U) {
      satisfied[index] = 1U;
    }
  }

  if (!matched) {
    return false;
  }

  if (ready_event->ready) {
    return false;
  }

  for (unsigned char index : satisfied) {
    if (index == 0U) {
      return false;
    }
  }

  ready_event->ready = true;
  ready_event->event.Trigger();
  return true;
}

auto ImportPlanner::AddTextureResource(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(
    PlanItemKind::kTextureResource, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddBufferResource(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(
    PlanItemKind::kBufferResource, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddAudioResource(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(
    PlanItemKind::kAudioResource, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddMaterialAsset(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(
    PlanItemKind::kMaterialAsset, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddGeometryAsset(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(
    PlanItemKind::kGeometryAsset, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddMeshBuild(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(PlanItemKind::kMeshBuild, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddSceneAsset(
  std::string debug_name, WorkPayloadHandle work_handle) -> PlanItemId
{
  return AddItem(PlanItemKind::kSceneAsset, std::move(debug_name), work_handle);
}

auto ImportPlanner::AddDependency(PlanItemId consumer, PlanItemId producer)
  -> void
{
  EnsureMutable();

  // These two statements are important as they validate the IDs.
  const auto u_consumer = ItemIndex(consumer);
  [[maybe_unused]] const auto u_producer = ItemIndex(producer);

  DLOG_F(INFO, "AddDependency consumer={} producer={}", u_consumer, u_producer);

  auto& consumer_deps = dependencies_.at(u_consumer);
  if (std::ranges::find(consumer_deps, producer) != consumer_deps.end()) {
    return;
  }

  consumer_deps.push_back(producer);
}

auto ImportPlanner::MakePlan() -> std::vector<PlanStep>
{
  EnsureMutable();
  sealed_ = true;

  size_t dependency_count = 0U;
  for (const auto& deps : dependencies_) {
    dependency_count += deps.size();
  }
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(INFO, "items: {}", items_.size());
  LOG_F(INFO, " deps: {}", dependency_count);

  for (const auto& item : items_) {
    const auto index = static_cast<size_t>(item.kind);
    CHECK_F(pipeline_registry_.at(index).has_value(),
      "Missing pipeline registration for plan item kind {}", index);
  }

  const auto item_count = items_.size();
  std::vector<size_t> in_degree(item_count, 0U);
  std::vector<std::vector<PlanItemId>> dependents(item_count);

  for (size_t consumer_index = 0; consumer_index < item_count;
    ++consumer_index) {
    const auto& deps = dependencies_[consumer_index];
    in_degree[consumer_index] = deps.size();

    for (const auto producer : deps) {
      const auto u_producer = ItemIndex(producer);
      dependents[u_producer].emplace_back(
        static_cast<uint32_t>(consumer_index));
    }
  }

  std::vector<size_t> ready_current;
  std::vector<size_t> ready_next;
  ready_current.reserve(item_count);
  ready_next.reserve(item_count);
  for (size_t index = 0; index < item_count; ++index) {
    if (in_degree[index] == 0U) {
      ready_current.push_back(index);
    }
  }

  std::vector<PlanItemId> order;
  order.reserve(item_count);

  std::ranges::sort(ready_current);

  while (!ready_current.empty()) {
    for (const auto current_index : ready_current) {
      order.emplace_back(static_cast<uint32_t>(current_index));
      const auto& item = items_.at(current_index);
      LOG_F(INFO, "{:>3}: id={:<3} {}/{}", order.size() - 1U, current_index,
        item.kind, item.debug_name);
      for (const auto dependent : dependents[current_index]) {
        const auto u_dependent = ItemIndex(dependent);
        DCHECK_F(in_degree[u_dependent] > 0U, "Invalid in-degree underflow");
        --in_degree[u_dependent];
        if (in_degree[u_dependent] == 0U) {
          ready_next.push_back(u_dependent);
        }
      }
    }

    ready_current.clear();
    if (!ready_next.empty()) {
      std::ranges::sort(ready_next);
      ready_current.swap(ready_next);
      ready_next.clear();
    }
  }

  CHECK_F(order.size() == item_count,
    "ImportPlanner cycle detected in dependency graph");

  events_.clear();
  events_.resize(item_count);
  trackers_.clear();
  trackers_.resize(item_count);
  required_storage_.clear();
  satisfied_storage_.clear();
  required_storage_.reserve(dependency_count);
  satisfied_storage_.reserve(dependency_count);

  for (size_t index = 0; index < item_count; ++index) {
    const auto& deps = dependencies_[index];
    const auto offset = required_storage_.size();
    required_storage_.insert(required_storage_.end(), deps.begin(), deps.end());
    satisfied_storage_.insert(satisfied_storage_.end(), deps.size(), 0U);

    auto& tracker = trackers_[index];
    tracker.required = std::span<const PlanItemId>(
      required_storage_.data() + offset, deps.size());
    tracker.satisfied
      = std::span(satisfied_storage_.data() + offset, deps.size());
    tracker.ready_event = &events_[index];

    if (deps.empty()) {
      tracker.ready_event->ready = true;
      tracker.ready_event->event.Trigger();
    }
  }

  std::vector<PlanStep> plan;
  plan.reserve(item_count);

  for (const auto item_id : order) {
    const auto u_item = ItemIndex(item_id);
    const auto& deps = dependencies_[u_item];
    plan.push_back(PlanStep {
      .item_id = item_id,
      .prerequisites = std::vector(deps.begin(), deps.end()),
    });
  }

  return plan;
}

auto ImportPlanner::Item(PlanItemId item) -> PlanItem&
{
  const auto u_item = ItemIndex(item);
  return items_.at(u_item);
}

auto ImportPlanner::PipelineTypeFor(PlanItemId item) const noexcept
  -> std::optional<TypeId>
{
  const auto u_item = ItemIndex(item);
  const auto index = static_cast<size_t>(items_.at(u_item).kind);
  return pipeline_registry_.at(index);
}

auto ImportPlanner::Tracker(PlanItemId item) -> ReadinessTracker&
{
  const auto u_item = ItemIndex(item);
  return trackers_.at(u_item);
}

auto ImportPlanner::ReadyEvent(PlanItemId item) -> ReadinessEvent&
{
  const auto u_item = ItemIndex(item);
  return events_.at(u_item);
}

auto ImportPlanner::AddItem(PlanItemKind kind, std::string debug_name,
  WorkPayloadHandle work_handle) -> PlanItemId
{
  EnsureMutable();

  const auto id = PlanItemId { static_cast<uint32_t>(items_.size()) };
  DLOG_F(INFO, "AddItem id={} kind={} name={}", id.get(), kind, debug_name);
  items_.push_back(PlanItem {
    .id = id,
    .kind = kind,
    .debug_name = std::move(debug_name),
    .work_handle = work_handle,
  });
  dependencies_.emplace_back();
  return id;
}

auto ImportPlanner::ItemIndex(PlanItemId item) const -> size_t
{
  const auto u_item = item.get();
  CHECK_F(u_item < items_.size(), "PlanItemId out of range: {}", u_item);
  return u_item;
}

auto ImportPlanner::EnsureMutable() const -> void
{
  CHECK_F(!sealed_, "ImportPlanner is sealed and cannot be modified");
}

} // namespace oxygen::content::import
