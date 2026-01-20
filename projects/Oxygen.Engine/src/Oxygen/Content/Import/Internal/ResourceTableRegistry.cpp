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

auto ResourceTableRegistry::FinalizeAll() -> co::Co<bool>
{
  std::vector<TextureTableAggregator*> textures;
  std::vector<BufferTableAggregator*> buffers;
  {
    std::lock_guard lock(mutex_);
    textures.reserve(texture_tables_.size());
    for (auto& [key, table] : texture_tables_) {
      textures.push_back(table.get());
    }
    buffers.reserve(buffer_tables_.size());
    for (auto& [key, table] : buffer_tables_) {
      buffers.push_back(table.get());
    }
  }

  bool ok = true;
  for (auto* table : textures) {
    if (!co_await table->Finalize()) {
      ok = false;
    }
  }
  for (auto* table : buffers) {
    if (!co_await table->Finalize()) {
      ok = false;
    }
  }

  {
    std::lock_guard lock(mutex_);
    texture_tables_.clear();
    buffer_tables_.clear();
  }

  co_return ok;
}

} // namespace oxygen::content::import
