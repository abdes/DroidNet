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
#include <Oxygen/Data/LooseCookedIndexFormat.h>
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
  return Emit(data::loose_cooked::FileKind::kTextureDescriptor, virtual_path,
    std::vector<std::byte>(data::EncodeTextureResourceDescriptor({
      .index = resource_index,
      .descriptor = descriptor,
    })));
}

auto ResourceDescriptorEmitter::EmitBuffer(const std::string_view virtual_path,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  const std::span<const data::BufferResourceView> views) -> std::string
{
  return Emit(data::loose_cooked::FileKind::kAuxiliary, virtual_path,
    data::EncodeBufferResourceDescriptor({
      .index = resource_index,
      .descriptor = descriptor,
      .views = internal::WithWholeBufferView(descriptor, views),
    }));
}

auto ResourceDescriptorEmitter::Emit(const data::loose_cooked::FileKind kind,
  const std::string_view virtual_path, std::vector<std::byte> bytes)
  -> std::string
{
  auto layout_relpath = std::string {};
  if (!internal::TryVirtualPathToRelPath(virtual_path, layout_relpath)) {
    throw std::invalid_argument("resource descriptor identity names no mount: "
      + std::string(virtual_path));
  }
  // Refused before anything is written, so no write escapes the cooked root.
  data::loose_cooked::RequireIndexRelPath(layout_relpath);
  const auto sha256 = base::ComputeSha256(bytes);
  if (const auto it = records_.find(std::string(virtual_path));
    it != records_.end()) {
    // A cook is deterministic: the same identity always has the same bytes.
    if (it->second.sha256 != sha256) {
      throw std::runtime_error("resource descriptor '"
        + std::string(virtual_path)
        + "' was emitted twice with different bytes");
    }
    return it->second.relpath;
  }
  auto relpath = data::loose_cooked::ContentRelPath(layout_relpath, sha256);
  records_.emplace(std::string(virtual_path),
    Record {
      .kind = kind,
      .relpath = relpath,
      .size_bytes = bytes.size(),
      .sha256 = sha256,
      .virtual_path = std::string(virtual_path),
    });
  QueueWrite(relpath, std::move(bytes));
  return relpath;
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
  // A file of this content name already holds these bytes.
  if (const auto size = storage_->SizeOf(full_path);
    size.has_value() && size->get() == bytes.size()) {
    co_return Result<void> {};
  }
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
