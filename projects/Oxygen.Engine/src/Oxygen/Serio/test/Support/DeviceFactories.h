//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>
#include <fmt/format.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/ByteView.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Testing/ScopedTempDir.h>

//! Conformance factories for Serio's own devices.
/*!
 Each factory makes a device holding given bytes and owns whatever the device
 needs (a temporary directory, backing memory, a loop and its driver).
 Composite factories (`AnyDeviceFactory`, `AnyAsyncDeviceFactory`,
 `FaultInjectingFactory`, `RegionFactory`) wrap another factory, so every
 composition of devices runs the same suites; they expose the loop of the
 factory they wrap.
*/
namespace oxygen::serio::test {

//! The value of a setup step that must succeed. A failure throws, which
//! GoogleTest reports as a failure of the running test.
template <class T> auto Required(Result<T> result, const char* what) -> T
{
  if (!result) {
    throw std::runtime_error(
      fmt::format("{} failed: {}", what, result.error().message()));
  }
  return std::move(result).value();
}

//! Throws when a setup step that must succeed fails; see `Required`.
inline auto Required(const Result<void>& result, const char* what) -> void
{
  if (!result) {
    throw std::runtime_error(
      fmt::format("{} failed: {}", what, result.error().message()));
  }
}

//! The files of one test: each `Write` creates a new file holding the bytes.
class TempFiles {
public:
  [[nodiscard]] auto Write(const std::span<const std::byte> bytes)
    -> std::filesystem::path
  {
    const auto path = dir_.File(fmt::format("file-{}.bin", next_++));
    auto file = Required(NativeFile::Open(path,
                           {
                             .access = Access::kWrite,
                             .creation = Creation::kCreateNew,
                           }),
      "create test file");
    Required(file.WriteAt(ByteOffset { 0 }, bytes), "write test file");
    return path;
  }

  //! A path in the test's directory, without creating the file.
  [[nodiscard]] auto Path(const std::string& name) const
    -> std::filesystem::path
  {
    return dir_.File(name);
  }

private:
  oxygen::testing::ScopedTempDir dir_;
  size_t next_ { 0 };
};

struct MemoryFileFactory {
  using Device = MemoryFile;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Required(MemoryFile::FromBytes(bytes), "MemoryFile::FromBytes");
  }
};

//! Owns the bytes each view reads.
struct ByteViewFactory {
  using Device = ByteView;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return ByteView(storage.emplace_back(bytes.begin(), bytes.end()));
  }

  std::vector<std::vector<std::byte>> storage;
};

struct NativeFileFactory {
  using Device = NativeFile;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Required(
      NativeFile::Open(files.Write(bytes), { .access = Access::kReadWrite }),
      "NativeFile::Open");
  }

  TempFiles files;
};

//! Files opened through a driver of the factory's own loop.
struct AsyncFileFactory {
  using Device = AsyncFile;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Required(
      driver.Open(files.Write(bytes), { .access = Access::kReadWrite }),
      "IoDriver::Open");
  }

  auto Loop() -> asio::io_context& { return loop; }

  asio::io_context loop;
  IoDriver driver = Required(IoDriver::Create(loop), "IoDriver::Create");
  TempFiles files;
};

//! Files opened through an inline driver: operations complete on the
//! awaiting thread. The factory still owns a loop, for running coroutines.
struct InlineAsyncFileFactory {
  using Device = AsyncFile;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Required(
      driver.Open(files.Write(bytes), { .access = Access::kReadWrite }),
      "IoDriver::Open");
  }

  auto Loop() -> asio::io_context& { return loop; }

  asio::io_context loop;
  IoDriver driver
    = Required(IoDriver::CreateInline(), "IoDriver::CreateInline");
  TempFiles files;
};

struct MappedFileFactory {
  using Device = MappedFile;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Required(MappedFile::Open(files.Write(bytes)), "MappedFile::Open");
  }

  TempFiles files;
};

//! Erases the devices another factory makes.
template <class Inner> struct AnyDeviceFactory {
  using Device = AnyDevice;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return AnyDevice(inner.Make(bytes));
  }

  Inner inner;
};

//! Erases the asynchronous devices another factory makes.
template <class Inner> struct AnyAsyncDeviceFactory {
  using Device = AnyAsyncDevice;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return AnyAsyncDevice(inner.Make(bytes));
  }

  auto Loop() -> asio::io_context&
    requires requires(Inner& factory) { factory.Loop(); }
  {
    return inner.Loop();
  }

  Inner inner;
};

//! Wraps the devices another factory makes in a fault-injecting device with
//! nothing scheduled, which must behave exactly as its inner device.
template <class Inner> struct FaultInjectingFactory {
  using Device = testing::FaultInjectingDevice<typename Inner::Device>;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    return Device(inner.Make(bytes));
  }

  auto Loop() -> asio::io_context&
    requires requires(Inner& factory) { factory.Loop(); }
  {
    return inner.Loop();
  }

  Inner inner;
};

//! Makes regions of the devices another factory makes: the content sits
//! between a prefix and a suffix that the region excludes.
template <class Inner> struct RegionFactory {
  using InnerDevice = Inner::Device;
  using Device = Region<InnerDevice>;

  static constexpr size_t kPadding = 37;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    auto padded = std::vector<std::byte>(bytes.size() + (2 * kPadding));
    std::ranges::copy(bytes, padded.begin() + kPadding);
    const auto& device = *devices.emplace_back(
      std::make_unique<InnerDevice>(inner.Make(padded)));
    return Required(Device::Make(device,
                      ByteRange {
                        .offset = ByteOffset { kPadding },
                        .count = CountOf(bytes.size()),
                      }),
      "Region::Make");
  }

  auto Loop() -> asio::io_context&
    requires requires(Inner& factory) { factory.Loop(); }
  {
    return inner.Loop();
  }

  Inner inner;
  //! Regions view devices; the devices stay put while the factory lives.
  std::vector<std::unique_ptr<InnerDevice>> devices;
};

} // namespace oxygen::serio::test
