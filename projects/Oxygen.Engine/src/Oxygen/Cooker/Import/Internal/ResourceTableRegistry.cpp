//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSessionToken.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Semaphore.h>

namespace oxygen::content::import {

namespace {

  constexpr auto kScriptsTableLockSuffix = std::string_view { "|scripts" };
  constexpr auto kScriptBindingsTableLockSuffix
    = std::string_view { "|script-bindings" };

} // namespace

struct ResourceTableRegistry::SharedTableLockState final {
  co::Semaphore semaphore { 1 };
};

ResourceTableRegistry::ResourceTableRegistry(IAsyncFileWriter& file_writer)
  : file_writer_(file_writer)
{
}

auto ResourceTableRegistry::NormalizeKey(
  const std::filesystem::path& cooked_root) const -> std::string
{
  return cooked_root.lexically_normal().string();
}

auto ResourceTableRegistry::TextureAggregator(
  const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
  -> TextureTableAggregator&
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  auto it = texture_tables_.find(key);
  if (it == texture_tables_.end()) {
    auto created = std::make_unique<TextureTableAggregator>(
      file_writer_, layout, cooked_root);
    it = texture_tables_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "Created texture table for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::BufferAggregator(
  const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
  -> BufferTableAggregator&
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  auto it = buffer_tables_.find(key);
  if (it == buffer_tables_.end()) {
    auto created = std::make_unique<BufferTableAggregator>(
      file_writer_, layout, cooked_root);
    it = buffer_tables_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "Created buffer table for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::PhysicsAggregator(
  const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
  -> PhysicsTableAggregator&
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  auto it = physics_tables_.find(key);
  if (it == physics_tables_.end()) {
    auto created = std::make_unique<PhysicsTableAggregator>(
      file_writer_, layout, cooked_root);
    it = physics_tables_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "Created physics table for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::LockScriptsTable(
  const std::filesystem::path& cooked_root) -> co::Co<SharedTableLockGuard>
{
  auto key = NormalizeKey(cooked_root);
  key.append(kScriptsTableLockSuffix);
  co_return co_await AcquireSharedTableLock(std::move(key));
}

auto ResourceTableRegistry::LockScriptBindingsTable(
  const std::filesystem::path& cooked_root) -> co::Co<SharedTableLockGuard>
{
  auto key = NormalizeKey(cooked_root);
  key.append(kScriptBindingsTableLockSuffix);
  co_return co_await AcquireSharedTableLock(std::move(key));
}

auto ResourceTableRegistry::BeginSession(
  const std::filesystem::path& cooked_root) -> ImportSessionToken
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  if (finalizing_.contains(key)) {
    throw std::logic_error(
      "Wait for resource table finalization before BeginSession");
  }
  auto participation = ImportSessionToken(this, key);
  auto& state = active_sessions_[key];
  ++state.active;
  return participation;
}

auto ResourceTableRegistry::AcquireSharedTableLock(std::string key)
  -> co::Co<SharedTableLockGuard>
{
  auto state = std::shared_ptr<SharedTableLockState> {};
  {
    std::scoped_lock lock(mutex_);
    auto& entry = shared_table_locks_[key];
    if (!entry) {
      entry = std::make_shared<SharedTableLockState>();
    }
    state = entry;
  }

  auto lock = co_await state->semaphore.Lock();
  co_return SharedTableLockGuard(std::move(state), std::move(lock));
}

auto ResourceTableRegistry::WaitForFinalization(
  const std::filesystem::path& cooked_root) -> co::Co<>
{
  const auto key = NormalizeKey(cooked_root);
  for (;;) {
    std::shared_ptr<co::Event> completion;
    {
      std::scoped_lock lock(mutex_);
      const auto found = finalizing_.find(key);
      if (found == finalizing_.end()) {
        co_return;
      }
      completion = found->second;
    }
    co_await *completion;
  }
}

auto ResourceTableRegistry::AbortSession(ImportSessionToken& participation)
  -> void
{
  participation.Validate(this);
  const auto& key = participation.key_;
  std::scoped_lock lock(mutex_);
  const auto found = active_sessions_.find(key);
  if (found == active_sessions_.end() || found->second.active == 0) {
    throw std::logic_error(
      "Abort resource session without matching BeginSession");
  }
  found->second.aborted = true;
  participation.active_ = false;
  if (--found->second.active == 0) {
    active_sessions_.erase(found);
    texture_tables_.erase(key);
    buffer_tables_.erase(key);
    physics_tables_.erase(key);
  }
}

auto ResourceTableRegistry::EndSession(ImportSessionToken& participation)
  -> co::Co<bool>
{
  participation.Validate(this);
  const auto& key = participation.key_;
  std::unique_ptr<TextureTableAggregator> textures;
  std::unique_ptr<BufferTableAggregator> buffers;
  std::unique_ptr<PhysicsTableAggregator> physics;
  std::shared_ptr<co::Event> completion;
  bool aborted = false;
  {
    std::scoped_lock lock(mutex_);
    const auto found = active_sessions_.find(key);
    if (found == active_sessions_.end() || found->second.active == 0) {
      throw std::logic_error(
        "End resource session without matching BeginSession");
    }
    if (found->second.active > 1) {
      --found->second.active;
      participation.active_ = false;
      co_return !found->second.aborted;
    }
    // Allocate the admission gate before consuming the final participation.
    completion = std::make_shared<co::Event>();
    finalizing_.emplace(key, completion);
    aborted = found->second.aborted;
    active_sessions_.erase(found);
    participation.active_ = false;
    if (auto node = texture_tables_.extract(key); !node.empty()) {
      textures = std::move(node.mapped());
    }
    if (auto node = buffer_tables_.extract(key); !node.empty()) {
      buffers = std::move(node.mapped());
    }
    if (auto node = physics_tables_.extract(key); !node.empty()) {
      physics = std::move(node.mapped());
    }
  }
  const auto release_admission = ScopeGuard([&]() noexcept {
    {
      std::scoped_lock lock(mutex_);
      finalizing_.erase(key);
    }
    completion->Trigger();
  });
  if (aborted) {
    co_return false;
  }
  bool ok = true;
  if (textures && !co_await textures->Finalize()) {
    ok = false;
  }
  if (buffers && !co_await buffers->Finalize()) {
    ok = false;
  }
  if (physics && !co_await physics->Finalize()) {
    ok = false;
  }
  co_return ok;
}

auto ResourceTableRegistry::FinalizeAll() -> co::Co<bool>
{
  std::unordered_map<std::string, std::unique_ptr<TextureTableAggregator>>
    textures;
  std::unordered_map<std::string, std::unique_ptr<BufferTableAggregator>>
    buffers;
  std::unordered_map<std::string, std::unique_ptr<PhysicsTableAggregator>>
    physics;
  {
    std::scoped_lock lock(mutex_);
    if (!active_sessions_.empty()) {
      LOG_F(
        WARNING, "Finalizing with {} active sessions", active_sessions_.size());
    }
    textures = std::move(texture_tables_);
    buffers = std::move(buffer_tables_);
    physics = std::move(physics_tables_);
    active_sessions_.clear();
  }

  bool ok = true;
  for (auto& table : textures | std::views::values) {
    if (table != nullptr) {
      if (!co_await table->Finalize()) {
        ok = false;
      }
    }
  }
  for (auto& table : buffers | std::views::values) {
    if (table != nullptr) {
      if (!co_await table->Finalize()) {
        ok = false;
      }
    }
  }
  for (auto& table : physics | std::views::values) {
    if (table != nullptr) {
      if (!co_await table->Finalize()) {
        ok = false;
      }
    }
  }

  co_return ok;
}

} // namespace oxygen::content::import
