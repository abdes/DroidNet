//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "Support/DeviceFactories.h"

#include <Oxygen/Serio/AnyStream.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>

//! Conformance factories for Serio's own streams.
/*!
 Input factories make a stream positioned at the start of given bytes; output
 factories make an empty stream and read back what reached its destination.
 Factories over devices take a device factory, so the same stream runs over
 memory in the unit tier and over files in the functional tier.
*/
namespace oxygen::serio::test {

//=== Input streams ===-----------------------------------------------------//

struct MemoryStreamInputFactory {
  using Stream = MemoryStream;

  auto Make(const std::span<const std::byte> bytes) -> Stream
  {
    auto stream = MemoryStream {};
    Required(stream.Write(bytes), "MemoryStream::Write");
    Required(stream.Seek(ByteOffset { 0 }), "MemoryStream::Seek");
    return stream;
  }
};

//! Owns the bytes each stream reads.
struct ReadOnlyMemoryStreamFactory {
  using Stream = ReadOnlyMemoryStream;

  auto Make(const std::span<const std::byte> bytes) -> Stream
  {
    return ReadOnlyMemoryStream(
      storage.emplace_back(bytes.begin(), bytes.end()));
  }

  std::vector<std::vector<std::byte>> storage;
};

//! Region read streams over the whole of each device a device factory makes.
template <class DeviceFactory> struct RegionReadStreamFactory {
  using Device = DeviceFactory::Device;
  using Stream = RegionReadStream<Device>;

  auto Make(const std::span<const std::byte> bytes) -> Stream
  {
    const auto& device
      = *devices.emplace_back(std::make_unique<Device>(inner.Make(bytes)));
    return Required(OpenReadStream(device), "OpenReadStream");
  }

  DeviceFactory inner;
  //! Streams view devices; the devices stay put while the factory lives.
  std::vector<std::unique_ptr<Device>> devices;
};

//! Region read streams over a device that returns at most a few bytes per
//! read, as a pipe or a network file may: streams must reassemble them.
struct ShortReadStreamFactory {
  using Device = testing::FaultInjectingDevice<MemoryFile>;
  using Stream = RegionReadStream<Device>;

  static constexpr ByteCount kMaxRead { 7 };

  auto Make(const std::span<const std::byte> bytes) -> Stream
  {
    auto& device = *devices.emplace_back(
      std::make_unique<Device>(MemoryFileFactory {}.Make(bytes)));
    device.LimitReads(kMaxRead);
    return Required(OpenReadStream(device), "OpenReadStream");
  }

  std::vector<std::unique_ptr<Device>> devices;
};

//! Erases the streams another input factory makes.
template <class Inner> struct AnyInputStreamFactory {
  using Stream = AnyInputStream;

  auto Make(const std::span<const std::byte> bytes) -> Stream
  {
    return AnyInputStream(inner.Make(bytes));
  }

  Inner inner;
};

//=== Output streams ===----------------------------------------------------//

struct MemoryStreamOutputFactory {
  using Stream = MemoryStream;

  auto Make() -> Stream { return MemoryStream {}; }

  auto Contents(Stream& stream) -> std::vector<std::byte>
  {
    const auto bytes = stream.Data();
    return { bytes.begin(), bytes.end() };
  }
};

//! A region write stream over an empty device a device factory makes; one
//! stream per factory.
template <class DeviceFactory> struct RegionWriteStreamFactory {
  using Device = DeviceFactory::Device;
  using Stream = RegionWriteStream<Device>;

  auto Make() -> Stream
  {
    device = std::make_unique<Device>(inner.Make({}));
    return Stream(*device);
  }

  auto Contents(Stream& /*stream*/) -> std::vector<std::byte>
  {
    return Written();
  }

  //! Everything the device holds.
  auto Written() -> std::vector<std::byte>
  {
    const auto size = Required(device->Size(), "Device::Size");
    auto bytes = std::vector<std::byte>(static_cast<size_t>(size.get()));
    Required(ReadExactlyAt(*device, ByteOffset { 0 }, bytes), "read back");
    return bytes;
  }

  DeviceFactory inner;
  std::unique_ptr<Device> device;
};

//! Erases the streams a region write stream factory makes.
template <class Inner> struct AnyOutputStreamFactory {
  using Stream = AnyOutputStream;

  auto Make() -> Stream { return AnyOutputStream(inner.Make()); }

  auto Contents(Stream& /*stream*/) -> std::vector<std::byte>
  {
    return inner.Written();
  }

  Inner inner;
};

} // namespace oxygen::serio::test
