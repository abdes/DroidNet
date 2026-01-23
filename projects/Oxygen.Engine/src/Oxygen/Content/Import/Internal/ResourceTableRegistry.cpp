//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>

namespace oxygen::content::import {

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

auto ResourceTableRegistry::BeginSession(
  const std::filesystem::path& cooked_root) -> void
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  auto& count = active_sessions_[key];
  ++count;
  DLOG_F(INFO, "Session started for '{}' (count={})", key, count);
}

auto ResourceTableRegistry::EndSession(const std::filesystem::path& cooked_root)
  -> co::Co<bool>
{
  const auto key = NormalizeKey(cooked_root);
  std::unique_ptr<TextureTableAggregator> textures;
  std::unique_ptr<BufferTableAggregator> buffers;
  uint32_t remaining = 0;
  {
    std::scoped_lock lock(mutex_);
    const auto it = active_sessions_.find(key);
    if (it == active_sessions_.end()) {
      LOG_F(WARNING, "End session without start for '{}'", key);
    } else if (it->second == 0) {
      LOG_F(WARNING, "Session count underflow for '{}'", key);
    } else {
      --it->second;
      remaining = it->second;
      if (remaining == 0) {
        active_sessions_.erase(it);
      }
    }

    if (remaining == 0) {
      if (const auto table_it = texture_tables_.find(key);
        table_it != texture_tables_.end()) {
        textures = std::move(table_it->second);
        texture_tables_.erase(table_it);
      }
      if (const auto table_it = buffer_tables_.find(key);
        table_it != buffer_tables_.end()) {
        buffers = std::move(table_it->second);
        buffer_tables_.erase(table_it);
      }
    }
  }

  if (remaining != 0) {
    co_return true;
  }

  bool ok = true;
  if (textures != nullptr) {
    if (!co_await textures->Finalize()) {
      ok = false;
    }
  }
  if (buffers != nullptr) {
    if (!co_await buffers->Finalize()) {
      ok = false;
    }
  }

  co_return ok;
}

auto ResourceTableRegistry::FinalizeAll() -> co::Co<bool>
{
  std::unordered_map<std::string, std::unique_ptr<TextureTableAggregator>>
    textures;
  std::unordered_map<std::string, std::unique_ptr<BufferTableAggregator>>
    buffers;
  {
    std::scoped_lock lock(mutex_);
    if (!active_sessions_.empty()) {
      LOG_F(
        WARNING, "Finalizing with {} active sessions", active_sessions_.size());
    }
    textures = std::move(texture_tables_);
    buffers = std::move(buffer_tables_);
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

  co_return ok;
}

} // namespace oxygen::content::import
