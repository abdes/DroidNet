//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Semaphore.h>

namespace oxygen::content::import {

class IAsyncFileWriter;

class ResourceTableRegistry final {
public:
  class SharedTableLockGuard;

  OXGN_COOK_API explicit ResourceTableRegistry(IAsyncFileWriter& file_writer);

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableRegistry)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableRegistry)

  OXGN_COOK_NDAPI auto TextureAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> TextureTableAggregator&;

  OXGN_COOK_NDAPI auto BufferAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> BufferTableAggregator&;

  OXGN_COOK_NDAPI auto PhysicsAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> PhysicsTableAggregator&;

  OXGN_COOK_NDAPI auto LockScriptsTable(
    const std::filesystem::path& cooked_root) -> co::Co<SharedTableLockGuard>;

  OXGN_COOK_NDAPI auto LockScriptBindingsTable(
    const std::filesystem::path& cooked_root) -> co::Co<SharedTableLockGuard>;

  //! Register an active import session for a cooked root.
  OXGN_COOK_API auto BeginSession(const std::filesystem::path& cooked_root)
    -> void;

  //! Complete a session and finalize tables if it was the last one.
  OXGN_COOK_API auto EndSession(const std::filesystem::path& cooked_root)
    -> co::Co<bool>;

  OXGN_COOK_NDAPI auto FinalizeAll() -> co::Co<bool>;

private:
  struct SharedTableLockState;

  [[nodiscard]] auto NormalizeKey(
    const std::filesystem::path& cooked_root) const -> std::string;

  [[nodiscard]] auto AcquireSharedTableLock(std::string key)
    -> co::Co<SharedTableLockGuard>;

  IAsyncFileWriter& file_writer_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<TextureTableAggregator>>
    texture_tables_;
  std::unordered_map<std::string, std::unique_ptr<BufferTableAggregator>>
    buffer_tables_;
  std::unordered_map<std::string, std::unique_ptr<PhysicsTableAggregator>>
    physics_tables_;
  std::unordered_map<std::string, std::shared_ptr<SharedTableLockState>>
    shared_table_locks_;
  std::unordered_map<std::string, uint32_t> active_sessions_;
};

class [[nodiscard]] ResourceTableRegistry::SharedTableLockGuard final {
public:
  SharedTableLockGuard() = default;

  SharedTableLockGuard(SharedTableLockGuard&&) noexcept = default;
  auto operator=(SharedTableLockGuard&&) noexcept
    -> SharedTableLockGuard& = default;

  SharedTableLockGuard(const SharedTableLockGuard&) = delete;
  auto operator=(const SharedTableLockGuard&) -> SharedTableLockGuard& = delete;

private:
  explicit SharedTableLockGuard(std::shared_ptr<SharedTableLockState> state,
    co::Semaphore::LockGuard lock) noexcept
    : state_(std::move(state))
    , lock_(std::move(lock))
  {
  }

  friend class ResourceTableRegistry;

  std::shared_ptr<SharedTableLockState> state_;
  co::Semaphore::LockGuard lock_ {};
};

} // namespace oxygen::content::import
