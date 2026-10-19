//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

ResourceDescriptorEmitter::ResourceDescriptorEmitter(
  IAsyncFileWriter& file_writer, const LooseCookedLayout& layout,
  std::filesystem::path cooked_root)
  : file_writer_(file_writer)
  , layout_(layout)
  , cooked_root_(std::move(cooked_root))
{
}

auto ResourceDescriptorEmitter::EmitTextureAtRelPath(
  const std::string_view relpath,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::TextureResourceDesc& descriptor) -> std::string
{
  const auto path = std::filesystem::path(relpath);
  if (path.empty() || path.has_root_path()
    || std::ranges::any_of(
      path, [](const auto& part) -> auto { return part == ".."; })) {
    throw std::invalid_argument(
      "texture descriptor path must remain within its cooked root");
  }

  auto bytes = std::make_shared<std::vector<std::byte>>(
    data::EncodeTextureResourceDescriptor({
      .index = resource_index,
      .descriptor = descriptor,
    }));
  records_.insert_or_assign(std::string(relpath),
    Record {
      .relpath = std::string(relpath),
      .size_bytes = bytes->size(),
      .virtual_path = layout_.DescriptorVirtualPath(relpath),
    });
  QueueWrite(std::string(relpath), std::move(bytes));
  return std::string(relpath);
}

auto ResourceDescriptorEmitter::EmitBufferAtRelPath(
  const std::string_view relpath,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  const std::span<const internal::BufferDescriptorView> views) -> std::string
{
  if (relpath.empty()) {
    throw std::runtime_error("buffer descriptor relpath must not be empty");
  }

  auto path = std::string(relpath);
  auto bytes = std::make_shared<std::vector<std::byte>>(
    internal::SerializeBufferDescriptorSidecar(
      resource_index, descriptor, views));
  records_.insert_or_assign(path,
    Record {
      .relpath = path,
      .size_bytes = bytes->size(),
      .virtual_path = {},
    });
  QueueWrite(std::move(path), std::move(bytes));
  return std::string(relpath);
}

auto ResourceDescriptorEmitter::QueueWrite(
  std::string relpath, std::shared_ptr<std::vector<std::byte>> bytes) -> void
{
  const auto full_path = cooked_root_ / std::filesystem::path(relpath);
  pending_count_.fetch_add(1, std::memory_order_acq_rel);
  const auto data = std::span<const std::byte>(*bytes);

  file_writer_.WriteAsync(full_path, data,
    WriteOptions {
      .create_directories = true,
      .overwrite = true,
      .share_write = true,
    },
    [this, relpath = std::move(relpath), bytes](const FileErrorInfo& error,
      [[maybe_unused]] uint64_t bytes_written) -> void {
      OnWriteComplete(relpath, error);
    });
}

auto ResourceDescriptorEmitter::OnWriteComplete(
  const std::string_view relpath, const FileErrorInfo& error) -> void
{
  pending_count_.fetch_sub(1, std::memory_order_acq_rel);
  if (!error.IsError()) {
    return;
  }

  error_count_.fetch_add(1, std::memory_order_acq_rel);
  if (!first_error_) {
    first_error_ = error;
  }
  LOG_F(ERROR, "resource descriptor write failed '{}': {}", relpath,
    error.ToString());
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

auto ResourceDescriptorEmitter::Finalize()
  -> co::Co<Result<void, FileErrorInfo>>
{
  const auto flush_result = co_await file_writer_.Flush();
  if (first_error_) {
    co_return Result<void, FileErrorInfo>::Err(*first_error_);
  }
  if (!flush_result) {
    co_return Result<void, FileErrorInfo>::Err(flush_result.error());
  }
  co_return Result<void, FileErrorInfo>::Ok();
}

} // namespace oxygen::content::import
