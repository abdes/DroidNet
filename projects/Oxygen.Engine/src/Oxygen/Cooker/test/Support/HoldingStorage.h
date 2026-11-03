//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::cooker::testing {

//! A storage whose awaited publications stop in flight until released, as a
//! write the OS has not completed yet.
/*!
 A held publication does not end on cancellation: a pending OS request ends
 only once the OS has released its buffer, so the nursery that started it
 joins it only after `Release`. Every other operation, and the publications
 `Holds` refuses, go to the storage it wraps. Tests use it to put a write in
 flight while the job that started it is cancelled or its tables are
 finalized.

 ```cpp
 HoldingStorage storage(RealCookerStorage());
 co_await storage.publication_held;   // a write is in flight
 job.Stop();                          // the job waits for it
 storage.Release();
 ```
*/
class HoldingStorage final : public content::cooked::ImportStorage {
public:
  //! Decides which publications are held; all of them when empty.
  using Filter = std::function<bool(const std::filesystem::path&)>;

  explicit HoldingStorage(
    content::cooked::ImportStorage& inner, Filter filter = {})
    : inner_(make_observer(&inner))
    , filter_(std::move(filter))
  {
  }

  ~HoldingStorage() override = default;

  OXYGEN_MAKE_NON_COPYABLE(HoldingStorage)
  OXYGEN_MAKE_NON_MOVABLE(HoldingStorage)

  //! Triggered when a publication is held in flight.
  co::Event publication_held;

  //! Publications started and not yet ended.
  [[nodiscard]] auto InFlight() const noexcept -> size_t { return in_flight_; }

  //! Lets the held publications, and the later ones, proceed.
  auto Release() -> void
  {
    hold_ = false;
    released_.Trigger();
  }

  [[nodiscard]] auto KindOf(const std::filesystem::path& path) const
    -> content::cooked::PathKind override
  {
    return inner_->KindOf(path);
  }

  [[nodiscard]] auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> override
  {
    return inner_->Open(path);
  }

  [[nodiscard]] auto List(const std::filesystem::path& path) const
    -> Result<std::vector<content::lc::RootEntry>> override
  {
    return inner_->List(path);
  }

  [[nodiscard]] auto Publish(const std::filesystem::path& path)
    -> Result<std::unique_ptr<content::cooked::Publication>> override
  {
    return inner_->Publish(path);
  }

  [[nodiscard]] auto PublishAsync(serio::IoDriver& driver,
    std::filesystem::path path, const std::span<const std::byte> bytes)
    -> co::Co<Result<void>> override
  {
    ++in_flight_;
    const auto ended = ScopeGuard([this] noexcept -> void { --in_flight_; });
    if (hold_ && (!filter_ || filter_(path))) {
      publication_held.Trigger();
      co_await co::NonCancellable([this] -> co::Co<> { co_await released_; });
    }
    co_return co_await inner_->PublishAsync(driver, std::move(path), bytes);
  }

  [[nodiscard]] auto OpenData(serio::IoDriver& driver,
    const std::filesystem::path& path) -> Result<serio::AnyAsyncDevice> override
  {
    return inner_->OpenData(driver, path);
  }

  [[nodiscard]] auto Remove(const std::filesystem::path& path)
    -> Result<void> override
  {
    return inner_->Remove(path);
  }

  [[nodiscard]] auto RemoveEmptyFolder(const std::filesystem::path& path)
    -> Result<bool> override
  {
    return inner_->RemoveEmptyFolder(path);
  }

  [[nodiscard]] auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> override
  {
    return inner_->Move(from, to);
  }

  [[nodiscard]] auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> override
  {
    return inner_->SizeOf(path);
  }

  [[nodiscard]] auto TryLock(
    const std::filesystem::path& path, const serio::FileLockMode mode) const
    -> Result<std::unique_ptr<content::cooked::StorageLock>> override
  {
    return inner_->TryLock(path, mode);
  }

  [[nodiscard]] auto Truncate(const std::filesystem::path& path,
    const serio::ByteCount size) -> Result<void> override
  {
    return inner_->Truncate(path, size);
  }

private:
  observer_ptr<content::cooked::ImportStorage> inner_;
  Filter filter_;
  co::Event released_;
  size_t in_flight_ = 0;
  bool hold_ = true;
};

} // namespace oxygen::cooker::testing
