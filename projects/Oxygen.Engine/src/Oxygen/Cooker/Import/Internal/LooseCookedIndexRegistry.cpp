//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/ImportSessionToken.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

auto LooseCookedIndexRegistry::NormalizeKey(
  const std::filesystem::path& cooked_root) const -> std::string
{
  return base::PathIdentityKey(cooked_root);
}

auto LooseCookedIndexRegistry::GetEntry(
  const std::filesystem::path& cooked_root) -> std::shared_ptr<Entry>
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);
  if (const auto found = entries_.find(key); found != entries_.end()) {
    return found->second;
  }
  auto state = std::make_shared<Entry>();
  entries_.emplace(key, state);
  return state;
}

auto LooseCookedIndexRegistry::BeginSession(
  const std::filesystem::path& cooked_root,
  const std::optional<data::SourceKey>& source_key) -> ImportSessionToken
{
  const auto key = NormalizeKey(cooked_root);
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
    if (source_key.has_value()) {
      entry.writer->SetSourceKey(source_key);
      entry.source_key = source_key;
    }
  } else if (source_key.has_value()) {
    if (!entry.source_key.has_value()) {
      entry.writer->SetSourceKey(source_key);
      entry.source_key = source_key;
    } else if (*entry.source_key != *source_key) {
      throw std::invalid_argument(
        "Concurrent imports disagree on the cooked root SourceKey");
    }
  }

  auto participation = ImportSessionToken(this, key);
  if (entry.active_sessions == 0) {
    entry.completion = std::make_shared<Completion>();
  }
  ++entry.active_sessions;
  return participation;
}

auto LooseCookedIndexRegistry::RegisterExternalFile(
  const std::filesystem::path& cooked_root,
  const data::loose_cooked::FileKind kind, std::string_view relpath) -> void
{
  const auto key = NormalizeKey(cooked_root);
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
  }

  entry.writer->RegisterExternalFile(kind, relpath);
  DLOG_F(INFO, "File '{}' kind={} registered for '{}'", std::string(relpath),
    static_cast<uint32_t>(kind), key);
}

auto LooseCookedIndexRegistry::RegisterExternalAssetDescriptor(
  const std::filesystem::path& cooked_root, const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const uint64_t descriptor_size,
  const std::optional<base::Sha256Digest>& descriptor_sha256) -> void
{
  const auto storage_key = NormalizeKey(cooked_root);
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
  }

  entry.writer->RegisterExternalAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, descriptor_size, descriptor_sha256);
  DLOG_F(INFO, "Asset '{}' type={} relpath='{}' registered for '{}'",
    data::to_string(key), static_cast<uint32_t>(asset_type),
    std::string(descriptor_relpath), storage_key);
}

auto LooseCookedIndexRegistry::EndSession(ImportSessionToken& participation)
  -> co::Co<Publication>
{
  participation.Validate(this);
  std::shared_ptr<Entry> state;
  {
    std::scoped_lock lock(mutex_);
    state = entries_.at(participation.key_);
  }
  std::shared_ptr<Completion> completion;
  std::optional<LooseCookedWriteResult> result;
  bool publishes = false;
  {
    std::scoped_lock lock(state->mutex);
    if (state->active_sessions == 0 || !state->writer) {
      throw std::logic_error("End index session without matching BeginSession");
    }
    completion = state->completion;
    --state->active_sessions;
    participation.active_ = false;
    publishes = state->active_sessions == 0;
    if (publishes) {
      auto writer = std::move(state->writer);
      state->source_key.reset();
      state->completion.reset();
      completion->aborted = state->aborted;
      if (!completion->aborted) {
        try {
          auto written = writer->Finish();
          completion->source_key = written.source_key;
          result = std::move(written);
        } catch (...) {
          completion->failure = std::current_exception();
        }
      }
    }
  }

  // Trigger resumes waiters synchronously; their callbacks may admit new work.
  if (publishes) {
    completion->ready.Trigger();
  }
  co_await completion->ready;
  if (completion->aborted) {
    throw std::runtime_error(
      "Index publication aborted by an interrupted import session");
  }
  if (completion->failure) {
    std::rethrow_exception(completion->failure);
  }
  co_return Publication {
    .source_key = completion->source_key,
    .write_result = std::move(result),
  };
}

auto LooseCookedIndexRegistry::AbortSession(ImportSessionToken& participation)
  -> void
{
  participation.Validate(this);
  std::shared_ptr<Entry> state;
  {
    std::scoped_lock lock(mutex_);
    state = entries_.at(participation.key_);
  }
  std::shared_ptr<Completion> completion;
  {
    std::scoped_lock lock(state->mutex);
    if (state->active_sessions == 0) {
      throw std::logic_error(
        "Abort index session without matching BeginSession");
    }
    state->aborted = true;
    participation.active_ = false;
    if (--state->active_sessions == 0) {
      state->writer.reset();
      state->source_key.reset();
      completion = std::move(state->completion);
      completion->aborted = true;
    }
  }
  if (completion) {
    completion->ready.Trigger();
  }
}

} // namespace oxygen::content::import
