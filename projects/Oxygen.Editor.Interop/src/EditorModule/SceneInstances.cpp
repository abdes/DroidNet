//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <algorithm>
#include <utility>

#include <EditorModule/SceneInstances.h>

namespace oxygen::interop::module {

namespace {
  //! The root an operation applies to.
  auto RootOf(const SceneInstances::Operation& operation) -> scene::NodeHandle
  {
    return std::visit([](const auto& value) { return value.root; }, operation);
  }

  //! Replaces the change with the address of `change`, or adds it.
  void Upsert(
    std::vector<HostInstanceChange>& changes, HostInstanceChange change)
  {
    const auto address = change.Address();
    const auto found = std::ranges::find_if(changes,
      [&address](const auto& entry) { return entry.Address() == address; });
    if (found != changes.end()) {
      *found = std::move(change);
      return;
    }
    changes.push_back(std::move(change));
  }
} // namespace

auto HostInstanceChange::Address() const -> data::ChangeAddress
{
  return data::ChangeAddress {
    .path = path,
    .target = data::PropertyAddress {
      .override_id = override_id,
      .part_id = part_id,
    },
  };
}

void SceneInstances::Instantiate(scene::NodeHandle root, std::string source,
  std::vector<HostInstanceChange> changes, FailureCallback on_failure,
  SuccessCallback on_success)
{
  Unqueue(root);
  requests_.insert_or_assign(root,
    Request {
      .source = std::move(source),
      .changes = std::move(changes),
      .on_failure = std::move(on_failure),
      .on_success = std::move(on_success),
      .built = 0,
      .build_queued = true,
    });
  queue_.emplace_back(BuildOperation { .root = root });
}

void SceneInstances::SetChange(
  scene::NodeHandle root, HostInstanceChange change)
{
  const auto found = requests_.find(root);
  if (found == requests_.end()) {
    return;
  }
  if (!found->second.build_queued) {
    queue_.emplace_back(SetOperation { .root = root, .change = change });
  }
  Upsert(found->second.changes, std::move(change));
}

void SceneInstances::ClearChange(
  scene::NodeHandle root, const data::ChangeAddress& address)
{
  const auto found = requests_.find(root);
  if (found == requests_.end()) {
    return;
  }
  std::erase_if(found->second.changes,
    [&address](const auto& entry) { return entry.Address() == address; });
  if (!found->second.build_queued) {
    queue_.emplace_back(ClearOperation { .root = root, .address = address });
  }
}

void SceneInstances::Detach(scene::NodeHandle root)
{
  Unqueue(root);
  requests_.erase(root);
  queue_.emplace_back(DetachOperation { .root = root });
}

void SceneInstances::Refresh()
{
  for (auto& [root, request] : requests_) {
    if (!request.build_queued) {
      request.build_queued = true;
      queue_.emplace_back(BuildOperation { .root = root });
    }
  }
}

void SceneInstances::Forget(scene::NodeHandle root)
{
  Unqueue(root);
  requests_.erase(root);
}

auto SceneInstances::TakeOperations() -> std::vector<Operation>
{
  auto operations = std::exchange(queue_, {});
  for (auto& operation : operations) {
    const auto generation = ++generation_;
    if (auto* build = std::get_if<BuildOperation>(&operation)) {
      auto& request = requests_.at(build->root);
      request.build_queued = false;
      request.built = generation;
      build->source = request.source;
      build->changes = request.changes;
      build->generation = generation;
    } else if (auto* set = std::get_if<SetOperation>(&operation)) {
      set->generation = generation;
    } else if (auto* clear = std::get_if<ClearOperation>(&operation)) {
      clear->generation = generation;
    }
  }
  return operations;
}

void SceneInstances::Report(scene::NodeHandle root,
  const std::uint64_t generation, const std::string_view error) const
{
  const auto found = requests_.find(root);
  if (found == requests_.end() || generation < found->second.built) {
    return;
  }
  const auto& request = found->second;
  if (!error.empty()) {
    if (request.on_failure) {
      request.on_failure(generation, std::string(error));
    }
  } else if (request.on_success) {
    request.on_success(generation);
  }
}

auto SceneInstances::Contains(scene::NodeHandle root) const -> bool
{
  return requests_.contains(root);
}

void SceneInstances::Unqueue(scene::NodeHandle root)
{
  std::erase_if(queue_, [&root](const auto& operation) {
    return RootOf(operation) == root;
  });
}

} // namespace oxygen::interop::module
