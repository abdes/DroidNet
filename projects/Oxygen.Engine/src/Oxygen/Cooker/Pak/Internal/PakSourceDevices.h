//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak::internal {

//! The source files of one pak build, each opened once.
/*!
 Every read of a source file in a build, a table, a payload slice or a
 descriptor, goes through here: the first read of a path opens it through the
 build's file port, and later reads take regions of the same device. Not
 thread safe; one build owns one.
*/
class PakSourceDevices final {
public:
  explicit PakSourceDevices(const cooked::CookerStorageReader& storage) noexcept
    : storage_(make_observer(&storage))
  {
  }

  //! The file port the build reads through.
  [[nodiscard]] auto Storage() const noexcept
    -> const cooked::CookerStorageReader&
  {
    return *storage_;
  }

  //! The size of the file `path`.
  [[nodiscard]] auto Size(const std::filesystem::path& path)
    -> Result<serio::ByteCount>;

  //! Fills `out` with the bytes at `at` of the file `path`.
  [[nodiscard]] auto ReadInto(const std::filesystem::path& path,
    serio::ByteOffset at, std::span<std::byte> out) -> Result<void>;

  //! The bytes `range` of the file `path`.
  [[nodiscard]] auto Read(const std::filesystem::path& path,
    serio::ByteRange range) -> Result<serio::ByteBuffer>;

private:
  [[nodiscard]] auto Device(const std::filesystem::path& path)
    -> Result<observer_ptr<const serio::AnyDevice>>;

  observer_ptr<const cooked::CookerStorageReader> storage_;
  std::map<std::filesystem::path, serio::AnyDevice> devices_;
};

} // namespace oxygen::content::pak::internal
