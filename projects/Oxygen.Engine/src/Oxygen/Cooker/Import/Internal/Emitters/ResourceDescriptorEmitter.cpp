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
#include <type_traits>
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
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

namespace {

  constexpr uint16_t kSidecarDescriptorVersion = 1;

  template <typename DescT, char C0, char C1, char C2, char C3>
#pragma pack(push, 1)
  struct SidecarDescriptorFile final {
    char magic[4] = { C0, C1, C2, C3 };
    uint16_t version = kSidecarDescriptorVersion;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    DescT descriptor {};
  };
#pragma pack(pop)

  using TextureSidecarFile
    = SidecarDescriptorFile<data::pak::core::TextureResourceDesc, 'O', 'T', 'E',
      'X'>;

  static_assert(std::is_trivially_copyable_v<TextureSidecarFile>);

  template <typename T>
  [[nodiscard]] auto SerializePod(const T& pod) -> std::vector<std::byte>
  {
    const auto bytes = std::as_bytes(std::span<const T, 1>(&pod, 1));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  }

} // namespace

ResourceDescriptorEmitter::ResourceDescriptorEmitter(
  IAsyncFileWriter& file_writer, const LooseCookedLayout& layout,
  std::filesystem::path cooked_root)
  : file_writer_(file_writer)
  , layout_(layout)
  , cooked_root_(std::move(cooked_root))
{
}

auto ResourceDescriptorEmitter::EmitTexture(std::string_view name_hint,
  std::string_view stable_id,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::TextureResourceDesc& descriptor) -> std::string
{
  return EmitTextureAtRelPath(
    layout_.TextureDescriptorRelPath(name_hint, stable_id), resource_index,
    descriptor);
}

auto ResourceDescriptorEmitter::EmitTextureAtRelPath(
  const std::string_view relpath,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::TextureResourceDesc& descriptor) -> std::string
{
  const auto path = std::filesystem::path(relpath);
  if (path.empty() || path.has_root_path()
    || std::ranges::any_of(
      path, [](const auto& part) { return part == ".."; })) {
    throw std::invalid_argument(
      "texture descriptor path must remain within its cooked root");
  }

  TextureSidecarFile file {};
  file.resource_index = resource_index;
  file.descriptor = descriptor;

  auto bytes = std::make_shared<std::vector<std::byte>>(SerializePod(file));
  record_sizes_.insert_or_assign(std::string(relpath), bytes->size());
  QueueWrite(std::string(relpath), std::move(bytes));
  return std::string(relpath);
}

auto ResourceDescriptorEmitter::EmitBuffer(std::string_view name_hint,
  std::string_view stable_id,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  const std::span<const internal::BufferDescriptorView> views) -> std::string
{
  auto relpath = layout_.BufferDescriptorRelPath(name_hint, stable_id);
  auto bytes = std::make_shared<std::vector<std::byte>>(
    internal::SerializeBufferDescriptorSidecar(
      resource_index, descriptor, views));
  record_sizes_[relpath] = bytes->size();
  QueueWrite(relpath, std::move(bytes));
  return relpath;
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
  record_sizes_[path] = bytes->size();
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
  records.reserve(record_sizes_.size());
  for (const auto& [relpath, size] : record_sizes_) {
    records.push_back(Record {
      .relpath = relpath,
      .size_bytes = size,
    });
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
