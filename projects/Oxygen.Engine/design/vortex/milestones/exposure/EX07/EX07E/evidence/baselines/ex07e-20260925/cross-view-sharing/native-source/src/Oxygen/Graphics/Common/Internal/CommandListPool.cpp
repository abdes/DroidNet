//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>

namespace oxygen::graphics::internal {
struct CommandPoolState {
  // Native/module retention outlasts both the factory and all idle lists.
  std::shared_ptr<void> native_lifetime;
  CommandListPool::CommandListFactory factory;
  std::mutex mutex;
  struct Bucket {
    std::vector<std::unique_ptr<CommandList>> idle;
    size_t total_created { 0 };
  };
  std::unordered_map<QueueRole, Bucket> buckets;
  bool closed { false };
};

CommandListPool::CommandListPool(
  CommandListFactory factory, std::shared_ptr<void> native_lifetime)
  : state_(std::make_shared<CommandPoolState>())
{
  if (!factory) {
    throw std::invalid_argument("CommandListPool requires a valid factory");
  }
  state_->factory = std::move(factory);
  state_->native_lifetime = std::move(native_lifetime);
}

CommandListPool::~CommandListPool() { Close(); }

auto CommandListPool::Clear() noexcept -> void
{
  std::lock_guard lock(state_->mutex);
  for (auto& [role, bucket] : state_->buckets) {
    bucket.total_created -= bucket.idle.size();
    bucket.idle.clear();
  }
}

auto CommandListPool::Close() noexcept -> void
{
  std::lock_guard lock(state_->mutex);
  state_->closed = true;
  state_->factory = {};
  for (auto& [role, bucket] : state_->buckets) {
    bucket.total_created -= bucket.idle.size();
    bucket.idle.clear();
  }
}

auto CommandListPool::SetNativeLifetime(std::shared_ptr<void> lifetime) -> void
{
  std::lock_guard lock(state_->mutex);
  if (state_->closed
    || std::ranges::any_of(state_->buckets,
      [](const auto& entry) { return entry.second.total_created != 0; })) {
    throw std::logic_error(
      "Cannot replace native lifetime of a used command pool");
  }
  state_->native_lifetime = std::move(lifetime);
}

auto CommandListPool::AcquireCommandList(QueueRole role, std::string_view name)
  -> std::shared_ptr<CommandList>
{
  std::unique_ptr<CommandList> list;
  {
    std::lock_guard lock(state_->mutex);
    if (state_->closed) {
      throw std::logic_error("Command list pool is closed");
    }
    auto& bucket = state_->buckets[role];
    if (bucket.idle.empty()) {
      if (bucket.idle.capacity() <= bucket.total_created) {
        bucket.idle.reserve(
          (std::max)(bucket.total_created + 1, bucket.idle.capacity() * 2));
      }
      list = state_->factory(role, name);
      if (!list) {
        return {};
      }
      ++bucket.total_created;
    } else {
      list = std::move(bucket.idle.back());
      bucket.idle.pop_back();
      list->SetName(name);
    }
  }
  auto* raw = list.release();
  return { raw, [state = state_, role](CommandList* returned) mutable noexcept {
            // Empty the stored deleter, including when weak list observers
            // survive.
            auto owner = std::move(state);
            std::unique_ptr<CommandList> value(returned);
            {
              std::lock_guard lock(owner->mutex);
              auto& bucket = owner->buckets.at(role);
              if (!owner->closed && returned->IsFree()) {
                assert(bucket.idle.size() < bucket.idle.capacity());
                bucket.idle.push_back(std::move(value));
              } else {
                --bucket.total_created;
              }
            }
            // A closed/invalid list is destroyed before its native lifetime
            // owner.
          } };
}
} // namespace oxygen::graphics::internal
