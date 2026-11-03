//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

ResourceDescriptorEmitter::ResourceDescriptorEmitter(
  cooked::ImportStorage& storage, serio::IoDriver& driver, co::Nursery& writes,
  std::filesystem::path cooked_root)
  : storage_(make_observer(&storage))
  , driver_(make_observer(&driver))
  , writes_(writes)
  , cooked_root_(std::move(cooked_root))
{
}

auto ResourceDescriptorEmitter::EmitTexture(const std::string_view virtual_path,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::TextureResourceDesc& descriptor) -> std::string
{
  auto relpath = std::string {};
  if (!internal::TryVirtualPathToRelPath(virtual_path, relpath)) {
    throw std::invalid_argument("texture descriptor identity names no mount: "
      + std::string(virtual_path));
  }
  // Refused before anything is written, so no write escapes the cooked root.
  data::loose_cooked::RequireIndexRelPath(relpath);
  auto bytes = std::vector<std::byte>(data::EncodeTextureResourceDescriptor({
    .index = resource_index,
    .descriptor = descriptor,
  }));
  if (RecordOnce(relpath, bytes, std::string(virtual_path))) {
    QueueWrite(relpath, std::move(bytes));
  }
  return relpath;
}

auto ResourceDescriptorEmitter::EmitBufferAtRelPath(
  const std::string_view relpath,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  const std::span<const data::BufferResourceView> views) -> std::string
{
  // Refused before anything is written, so no write escapes the cooked root.
  data::loose_cooked::RequireIndexRelPath(relpath);
  auto path = std::string(relpath);
  auto bytes = data::EncodeBufferResourceDescriptor({
    .index = resource_index,
    .descriptor = descriptor,
    .views = internal::WithWholeBufferView(descriptor, views),
  });
  if (RecordOnce(path, bytes, {})) {
    QueueWrite(std::move(path), std::move(bytes));
  }
  return std::string(relpath);
}

auto ResourceDescriptorEmitter::RecordOnce(const std::string& relpath,
  const std::span<const std::byte> bytes, std::string virtual_path) -> bool
{
  const auto sha256 = base::ComputeSha256(bytes);
  if (const auto it = records_.find(relpath); it != records_.end()) {
    // A cook is deterministic: the same descriptor path always holds the same
    // bytes. Writing it again could finish before the first write, leaving
    // bytes the recorded digest does not describe.
    if (it->second.size_bytes != bytes.size() || it->second.sha256 != sha256) {
      throw std::runtime_error("resource descriptor '" + relpath
        + "' was emitted twice with different bytes");
    }
    return false;
  }
  records_.emplace(relpath,
    Record {
      .relpath = relpath,
      .size_bytes = bytes.size(),
      .sha256 = sha256,
      .virtual_path = std::move(virtual_path),
    });
  return true;
}

auto ResourceDescriptorEmitter::QueueWrite(
  std::string relpath, std::vector<std::byte> bytes) -> void
{
  auto path = cooked_root_ / std::filesystem::path(relpath);
  writes_.Start(
    std::move(path), WriteDescriptor(std::move(relpath), std::move(bytes)));
}

auto ResourceDescriptorEmitter::WriteDescriptor(const std::string relpath,
  std::vector<std::byte> bytes) -> co::Co<Result<void>>
{
  const auto full_path = cooked_root_ / std::filesystem::path(relpath);
  const auto written = co_await storage_->PublishAsync(
    *driver_, full_path, std::span<const std::byte>(bytes));
  if (!written) {
    LOG_F(ERROR, "resource descriptor write failed '{}' ({}): {}", relpath,
      full_path.string(), written.error().message());
  }
  co_return written;
}

auto ResourceDescriptorEmitter::Records() const -> std::vector<Record>
{
  auto records = std::vector<Record> {};
  records.reserve(records_.size());
  for (const auto& [relpath, record] : records_) {
    static_cast<void>(relpath);
    records.push_back(record);
  }
  std::ranges::sort(records, [](const Record& lhs, const Record& rhs) -> bool {
    return lhs.relpath < rhs.relpath;
  });
  return records;
}

auto ResourceDescriptorEmitter::Finalize() -> co::Co<Result<void, WriteFailure>>
{
  co_return co_await writes_.Join();
}

} // namespace oxygen::content::import
