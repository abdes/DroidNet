//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h>

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

  [[nodiscard]] auto Fnv1a64(const std::string_view text) -> uint64_t
  {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    auto hash = kFnvOffsetBasis;
    for (const auto c : text) {
      hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
      hash *= kFnvPrime;
    }
    return hash;
  }

  [[nodiscard]] auto Hex64(const uint64_t value) -> std::string
  {
    constexpr std::array kDigits {
      '0',
      '1',
      '2',
      '3',
      '4',
      '5',
      '6',
      '7',
      '8',
      '9',
      'a',
      'b',
      'c',
      'd',
      'e',
      'f',
    };

    auto out = std::string(16, '0');
    for (size_t i = 0; i < out.size(); ++i) {
      const auto shift = static_cast<unsigned>((out.size() - 1U - i) * 4U);
      out[i] = kDigits[(value >> shift) & 0xFU];
    }
    return out;
  }

  [[nodiscard]] auto ExtractLeafStem(const std::string_view text) -> std::string
  {
    auto normalized = std::string(text);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const auto slash_pos = normalized.find_last_of('/');
    auto leaf = (slash_pos == std::string::npos)
      ? normalized
      : normalized.substr(slash_pos + 1U);

    const auto dot_pos = leaf.find_last_of('.');
    if (dot_pos != std::string::npos) {
      leaf.resize(dot_pos);
    }

    return leaf;
  }

  [[nodiscard]] auto SanitizeStem(
    std::string stem, const std::string_view fallback) -> std::string
  {
    if (stem.empty()) {
      stem = std::string(fallback);
    }

    std::string out;
    out.reserve(stem.size());

    auto last_was_underscore = false;
    for (const auto ch : stem) {
      const auto u = static_cast<unsigned char>(ch);
      const bool keep = std::isalnum(u) != 0 || ch == '_' || ch == '-';
      const char normalized = keep ? static_cast<char>(ch) : '_';
      if (normalized == '_') {
        if (!last_was_underscore) {
          out.push_back('_');
        }
        last_was_underscore = true;
      } else {
        out.push_back(normalized);
        last_was_underscore = false;
      }
    }

    while (!out.empty() && out.front() == '_') {
      out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
      out.pop_back();
    }

    if (out.empty()) {
      return std::string(fallback);
    }
    return out;
  }

  [[nodiscard]] auto BuildStem(std::string_view name_hint,
    std::string_view stable_id, std::string_view fallback) -> std::string
  {
    const auto base = SanitizeStem(ExtractLeafStem(name_hint), fallback);
    const auto id_source = stable_id.empty() ? name_hint : stable_id;
    return base + "_" + Hex64(Fnv1a64(id_source));
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
  TextureSidecarFile file {};
  file.resource_index = resource_index;
  file.descriptor = descriptor;

  const auto stem = BuildStem(name_hint, stable_id, "texture");
  auto relpath = layout_.TextureDescriptorRelPath(stem);
  auto bytes = std::make_shared<std::vector<std::byte>>(SerializePod(file));
  record_sizes_[relpath] = bytes->size();
  QueueWrite(std::move(relpath), std::move(bytes));
  return layout_.TextureDescriptorRelPath(stem);
}

auto ResourceDescriptorEmitter::EmitBuffer(std::string_view name_hint,
  std::string_view stable_id,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::core::BufferResourceDesc& descriptor,
  const std::span<const internal::BufferDescriptorView> views) -> std::string
{
  const auto stem = BuildStem(name_hint, stable_id, "buffer");
  auto relpath = layout_.BufferDescriptorRelPath(stem);
  auto bytes = std::make_shared<std::vector<std::byte>>(
    internal::SerializeBufferDescriptorSidecar(
      resource_index, descriptor, views));
  record_sizes_[relpath] = bytes->size();
  QueueWrite(std::move(relpath), std::move(bytes));
  return layout_.BufferDescriptorRelPath(stem);
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

auto ResourceDescriptorEmitter::EmitPhysicsResource(std::string_view name_hint,
  std::string_view stable_id,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::physics::PhysicsResourceDesc& descriptor) -> std::string
{
  const auto stem = BuildStem(name_hint, stable_id, "physics_resource");
  auto relpath = layout_.PhysicsResourceDescriptorRelPath(stem);
  auto bytes = std::make_shared<std::vector<std::byte>>(
    internal::SerializePhysicsResourceDescriptorSidecar(
      resource_index, descriptor));
  record_sizes_[relpath] = bytes->size();
  QueueWrite(std::move(relpath), std::move(bytes));
  return layout_.PhysicsResourceDescriptorRelPath(stem);
}

auto ResourceDescriptorEmitter::EmitPhysicsResourceAtRelPath(
  const std::string_view relpath,
  const data::pak::core::ResourceIndexT resource_index,
  const data::pak::physics::PhysicsResourceDesc& descriptor) -> std::string
{
  if (relpath.empty()) {
    throw std::runtime_error(
      "physics resource descriptor relpath must not be empty");
  }

  auto path = std::string(relpath);
  auto bytes = std::make_shared<std::vector<std::byte>>(
    internal::SerializePhysicsResourceDescriptorSidecar(
      resource_index, descriptor));
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
    [this, relpath = std::move(relpath), bytes](
      const FileErrorInfo& error, [[maybe_unused]] uint64_t bytes_written) {
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
  std::ranges::sort(records, [](const Record& lhs, const Record& rhs) {
    return lhs.relpath < rhs.relpath;
  });
  return records;
}

auto ResourceDescriptorEmitter::Finalize() -> co::Co<bool>
{
  auto flush_result = co_await file_writer_.Flush();
  if (!flush_result.has_value()) {
    LOG_F(ERROR, "resource descriptor emitter flush failed: {}",
      flush_result.error().ToString());
    co_return false;
  }

  if (error_count_.load(std::memory_order_acquire) > 0U) {
    co_return false;
  }
  co_return true;
}

} // namespace oxygen::content::import
