//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>

#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>

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
  std::lock_guard lock(mutex_);
  auto it = texture_tables_.find(key);
  if (it == texture_tables_.end()) {
    auto created = std::make_unique<TextureTableAggregator>(
      file_writer_, layout, cooked_root);
    it = texture_tables_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "ResourceTableRegistry: created texture table for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::BufferAggregator(
  const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
  -> BufferTableAggregator&
{
  const auto key = NormalizeKey(cooked_root);
  std::lock_guard lock(mutex_);
  auto it = buffer_tables_.find(key);
  if (it == buffer_tables_.end()) {
    auto created = std::make_unique<BufferTableAggregator>(
      file_writer_, layout, cooked_root);
    it = buffer_tables_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "ResourceTableRegistry: created buffer table for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::FinalizeGateForRoot(
  const std::filesystem::path& cooked_root) -> co::Semaphore&
{
  const auto key = NormalizeKey(cooked_root);
  std::lock_guard lock(mutex_);
  auto it = finalize_gates_.find(key);
  if (it == finalize_gates_.end()) {
    auto created = std::make_unique<co::Semaphore>(1);
    it = finalize_gates_.emplace(key, std::move(created)).first;
    DLOG_F(INFO, "ResourceTableRegistry: created finalize gate for '{}'", key);
  }
  return *it->second;
}

auto ResourceTableRegistry::FinalizeForRoot(
  const std::filesystem::path& cooked_root) -> co::Co<bool>
{
  const auto key = NormalizeKey(cooked_root);
  TextureTableAggregator* textures = nullptr;
  BufferTableAggregator* buffers = nullptr;
  {
    std::lock_guard lock(mutex_);
    if (const auto it = texture_tables_.find(key);
      it != texture_tables_.end()) {
      textures = it->second.get();
    }
    if (const auto it = buffer_tables_.find(key); it != buffer_tables_.end()) {
      buffers = it->second.get();
    }
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

  {
    std::lock_guard lock(mutex_);
    texture_tables_.erase(key);
    buffer_tables_.erase(key);
  }

  co_return ok;
}

auto ResourceTableRegistry::FinalizeAll() -> co::Co<bool>
{
  std::vector<std::string> keys;
  {
    std::lock_guard lock(mutex_);
    keys.reserve(texture_tables_.size() + buffer_tables_.size());
    for (const auto& [key, table] : texture_tables_) {
      keys.push_back(key);
    }
    for (const auto& [key, table] : buffer_tables_) {
      keys.push_back(key);
    }
  }

  bool ok = true;
  for (const auto& key : keys) {
    if (!co_await FinalizeForRoot(std::filesystem::path(key))) {
      ok = false;
    }
  }

  co_return ok;
}

} // namespace oxygen::content::import
