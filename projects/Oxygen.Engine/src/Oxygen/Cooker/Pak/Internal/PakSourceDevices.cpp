//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Pak/Internal/PakSourceDevices.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak::internal {

auto PakSourceDevices::Device(const std::filesystem::path& path)
  -> Result<observer_ptr<const serio::AnyDevice>>
{
  const auto key = path.lexically_normal();
  if (const auto found = devices_.find(key); found != devices_.end()) {
    return ::oxygen::Ok(make_observer(&std::as_const(found->second)));
  }
  auto device = storage_->Open(key);
  if (!device) {
    return ::oxygen::Err(device.error());
  }
  const auto [inserted, _] = devices_.emplace(key, std::move(device).value());
  return ::oxygen::Ok(make_observer(&std::as_const(inserted->second)));
}

auto PakSourceDevices::Size(const std::filesystem::path& path)
  -> Result<serio::ByteCount>
{
  const auto device = Device(path);
  if (!device) {
    return ::oxygen::Err(device.error());
  }
  return (*device)->Size();
}

auto PakSourceDevices::ReadInto(const std::filesystem::path& path,
  const serio::ByteOffset at, const std::span<std::byte> out) -> Result<void>
{
  const auto device = Device(path);
  if (!device) {
    return ::oxygen::Err(device.error());
  }
  return serio::ReadExactlyAt(**device, at, out);
}

auto PakSourceDevices::Read(const std::filesystem::path& path,
  const serio::ByteRange range) -> Result<serio::ByteBuffer>
{
  const auto device = Device(path);
  if (!device) {
    return ::oxygen::Err(device.error());
  }
  const auto region = serio::Region<serio::AnyDevice>::Make(**device, range);
  if (!region) {
    return ::oxygen::Err(region.error());
  }
  return serio::ReadAll(*region, range.count);
}

} // namespace oxygen::content::pak::internal
