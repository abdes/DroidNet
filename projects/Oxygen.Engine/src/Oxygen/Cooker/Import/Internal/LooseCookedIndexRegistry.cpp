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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/ImportSessionToken.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Semaphore.h>

namespace oxygen::content::import {

LooseCookedIndexRegistry::LooseCookedIndexRegistry(
  observer_ptr<cooked::ImportStorage> storage)
  : storage_(storage)
{
  CHECK_NOTNULL_F(storage_, "LooseCookedIndexRegistry requires a file system");
}

auto LooseCookedIndexRegistry::Storage() const noexcept
  -> observer_ptr<cooked::ImportStorage>
{
  return storage_;
}

auto LooseCookedIndexRegistry::LockDescriptor(std::filesystem::path cooked_root,
  std::string virtual_path) -> co::Co<DescriptorEditGuard>
{
  const auto entry = GetEntry(cooked_root);
  std::shared_ptr<co::Semaphore> semaphore;
  {
    const std::scoped_lock lock(entry->mutex);
    auto& slot = entry->descriptor_locks[virtual_path];
    if (!slot) {
      slot = std::make_shared<co::Semaphore>(1);
    }
    semaphore = slot;
  }
  auto lock = co_await semaphore->Lock();
  co_return DescriptorEditGuard(std::move(semaphore), std::move(lock));
}

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
  // Only this map still holds an idle root's entry, and nothing can reach it
  // again: forget those, so a long-lived service does not grow per root.
  std::erase_if(entries_, [](const auto& entry) -> bool {
    return entry.second.use_count() == 1 && !entry.second->writer
      && entry.second->active_sessions == 0;
  });
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
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
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
  const data::loose_cooked::FileKind kind, std::string_view relpath,
  const LooseCookedFileDigest& produced) -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RegisterExternalFile(kind, relpath, produced);
  DLOG_F(INFO, "File '{}' kind={} registered for '{}'", std::string(relpath),
    static_cast<uint32_t>(kind), NormalizeKey(cooked_root));
}

auto LooseCookedIndexRegistry::RegisterExternalDataFile(
  const std::filesystem::path& cooked_root,
  const data::loose_cooked::FileKind kind, std::string_view relpath) -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RegisterExternalDataFile(kind, relpath);
  DLOG_F(INFO, "Data file '{}' kind={} registered for '{}'",
    std::string(relpath), static_cast<uint32_t>(kind),
    NormalizeKey(cooked_root));
}

auto LooseCookedIndexRegistry::RegisterExternalTextureDescriptor(
  const std::filesystem::path& cooked_root, std::string_view relpath,
  std::string_view virtual_path, const LooseCookedProducer& producer,
  const LooseCookedFileDigest& produced) -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RegisterExternalTextureDescriptor(
    relpath, virtual_path, producer, produced);
  DLOG_F(INFO, "Texture descriptor '{}' ({}) registered for '{}'",
    std::string(relpath), std::string(virtual_path), NormalizeKey(cooked_root));
}

auto LooseCookedIndexRegistry::RegisterExternalAuxiliaryFile(
  const std::filesystem::path& cooked_root, std::string_view relpath,
  std::string_view virtual_path, const LooseCookedProducer& producer,
  const LooseCookedFileDigest& produced) -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RegisterExternalAuxiliaryFile(
    relpath, virtual_path, producer, produced);
  DLOG_F(INFO, "Auxiliary file '{}' registered for '{}'", std::string(relpath),
    NormalizeKey(cooked_root));
}

auto LooseCookedIndexRegistry::RetireUnwrittenOutputs(
  const std::filesystem::path& cooked_root, const LooseCookedRecook& recook)
  -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RetireUnwrittenOutputs(recook);
  DLOG_F(INFO, "Outputs '{}' no longer writes under '{}' retire from '{}'",
    recook.source, recook.identity, NormalizeKey(cooked_root));
}

auto LooseCookedIndexRegistry::RegisterProducedAssetDescriptor(
  const std::filesystem::path& cooked_root, const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const LooseCookedFileDigest& produced,
  const data::AssetReferences& references, const LooseCookedProducer& producer,
  const std::span<const data::loose_cooked::IndexedRequirement> requirements)
  -> void
{
  const auto state = GetEntry(cooked_root);
  std::scoped_lock lock(state->mutex);
  auto& entry = *state;
  if (!entry.writer) {
    entry.aborted = false;
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root, *storage_);
  }

  entry.writer->RegisterProducedAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, produced, references, producer, requirements);
  DLOG_F(INFO, "Asset '{}' type={} relpath='{}' registered for '{}'",
    data::to_string(key), static_cast<uint32_t>(asset_type),
    std::string(descriptor_relpath), NormalizeKey(cooked_root));
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
  bool closes = false;
  {
    std::scoped_lock lock(state->mutex);
    if (state->active_sessions == 0 || !state->writer) {
      throw std::logic_error("End index session without matching BeginSession");
    }
    completion = state->completion;
    state->commit_requested = true;
    --state->active_sessions;
    participation.active_ = false;
    closes = state->active_sessions == 0;
    if (closes) {
      result = CloseCohort(*state);
    }
  }

  // Trigger resumes waiters synchronously; their callbacks may admit new work.
  if (closes) {
    completion->ready.Trigger();
  }
  co_await completion->ready;
  if (completion->aborted) {
    throw LooseCookedPublicationAborted(
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

auto LooseCookedIndexRegistry::CloseCohort(Entry& state)
  -> std::optional<LooseCookedWriteResult>
{
  auto writer = std::move(state.writer);
  auto completion = std::move(state.completion);
  const auto commits = state.commit_requested && !state.aborted;
  state.source_key.reset();
  state.commit_requested = false;
  completion->aborted = state.aborted;
  if (!commits) {
    return std::nullopt;
  }
  try {
    auto written = writer->Finish();
    completion->source_key = written.source_key;
    return written;
  } catch (...) {
    completion->failure = std::current_exception();
    return std::nullopt;
  }
}

auto LooseCookedIndexRegistry::LeaveSession(ImportSessionToken& participation)
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
    if (state->active_sessions == 0 || !state->writer) {
      throw std::logic_error(
        "Leave index session without matching BeginSession");
    }
    participation.active_ = false;
    if (--state->active_sessions == 0) {
      completion = state->completion;
      static_cast<void>(CloseCohort(*state));
    }
  }
  if (completion) {
    completion->ready.Trigger();
  }
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
      state->commit_requested = false;
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
