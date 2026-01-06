//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/ResourceAppender.h>

#include <cstring>
#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/util/Signature.h>
#include <Oxygen/Content/Import/util/TextureRepack.h>

namespace oxygen::content::import::emit {

auto TryReadWholeFileBytes(const std::filesystem::path& path)
  -> std::optional<std::vector<std::byte>>
{
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)
    || !std::filesystem::is_regular_file(path, ec)) {
    return std::nullopt;
  }

  const auto size_u64 = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::nullopt;
  }
  if (size_u64 == 0) {
    return std::vector<std::byte> {};
  }
  if (size_u64 > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    return std::nullopt;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return std::nullopt;
  }

  std::vector<std::byte> bytes;
  bytes.resize(static_cast<size_t>(size_u64));
  stream.read(reinterpret_cast<char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));
  if (!stream.good() && !stream.eof()) {
    return std::nullopt;
  }
  return bytes;
}

auto AppendResource(ResourceAppender& appender,
  std::span<const std::byte> bytes, uint64_t alignment) -> uint64_t
{
  using util::AlignUp;

  // Calculate aligned offset
  const auto aligned_offset = AlignUp(appender.current_offset, alignment);

  // Open stream lazily
  if (!appender.stream.has_value()) {
    // Create parent directory if it doesn't exist
    const auto parent_dir = appender.data_path.parent_path();
    if (!parent_dir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent_dir, ec);
      // Ignore error - opening the file will fail if directory creation failed
    }

    appender.stream.emplace(
      appender.data_path, std::ios::binary | std::ios::app);
    if (!appender.stream->is_open()) {
      throw std::runtime_error(
        "Failed to open data file for append: " + appender.data_path.string());
    }
  }

  // Write padding if needed
  if (aligned_offset > appender.current_offset) {
    const auto padding_size
      = static_cast<size_t>(aligned_offset - appender.current_offset);
    std::vector<std::byte> padding(padding_size, std::byte { 0 });
    appender.stream->write(reinterpret_cast<const char*>(padding.data()),
      static_cast<std::streamsize>(padding.size()));
  }

  // Write data
  appender.stream->write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));

  const auto write_offset = aligned_offset;
  appender.current_offset = aligned_offset + bytes.size();

  return write_offset;
}

auto CloseAppender(ResourceAppender& appender) -> void
{
  if (appender.stream.has_value()) {
    appender.stream->flush();
    appender.stream->close();
    appender.stream.reset();
  }
}

auto InitTextureEmissionState(const std::filesystem::path& table_path,
  const std::filesystem::path& data_path) -> TextureEmissionState
{
  using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;

  TextureEmissionState state;
  state.appender.data_path = data_path;

  std::error_code ec;

  // Check if files exist
  const bool table_exists = std::filesystem::exists(table_path, ec)
    && std::filesystem::is_regular_file(table_path, ec);
  const bool data_exists = std::filesystem::exists(data_path, ec)
    && std::filesystem::is_regular_file(data_path, ec);

  if (!table_exists && !data_exists) {
    // Fresh start
    state.appender.current_offset = 0;
    return state;
  }

  if (table_exists != data_exists) {
    throw std::runtime_error(
      "Existing cooked root has mismatched textures.table/textures.data");
  }

  // Load table (small file)
  const auto table_bytes_opt = TryReadWholeFileBytes(table_path);
  if (!table_bytes_opt.has_value()) {
    throw std::runtime_error("Failed to read textures.table");
  }

  const auto& table_bytes = *table_bytes_opt;
  if (table_bytes.size() % sizeof(TextureResourceDesc) != 0U) {
    throw std::runtime_error(
      "textures.table size is not a multiple of TextureResourceDesc");
  }

  const auto count = table_bytes.size() / sizeof(TextureResourceDesc);
  state.table.resize(count);
  if (!table_bytes.empty()) {
    std::memcpy(state.table.data(), table_bytes.data(), table_bytes.size());
  }

  // Get data file size without loading it
  state.appender.current_offset = std::filesystem::file_size(data_path, ec);
  if (ec) {
    throw std::runtime_error("Failed to get textures.data file size");
  }

  LOG_F(INFO, "Loaded existing textures: count={} data_size={}",
    state.table.size(), state.appender.current_offset);

  return state;
}

auto InitBufferEmissionState(const std::filesystem::path& table_path,
  const std::filesystem::path& data_path) -> BufferEmissionState
{
  using BufferResourceDesc = oxygen::data::pak::BufferResourceDesc;

  BufferEmissionState state;
  state.appender.data_path = data_path;

  std::error_code ec;

  const bool table_exists = std::filesystem::exists(table_path, ec)
    && std::filesystem::is_regular_file(table_path, ec);
  const bool data_exists = std::filesystem::exists(data_path, ec)
    && std::filesystem::is_regular_file(data_path, ec);

  if (!table_exists && !data_exists) {
    state.appender.current_offset = 0;
    return state;
  }

  if (table_exists != data_exists) {
    throw std::runtime_error(
      "Existing cooked root has mismatched buffers.table/buffers.data");
  }

  const auto table_bytes_opt = TryReadWholeFileBytes(table_path);
  if (!table_bytes_opt.has_value()) {
    throw std::runtime_error("Failed to read buffers.table");
  }

  const auto& table_bytes = *table_bytes_opt;
  if (table_bytes.size() % sizeof(BufferResourceDesc) != 0U) {
    throw std::runtime_error(
      "buffers.table size is not a multiple of BufferResourceDesc");
  }

  const auto count = table_bytes.size() / sizeof(BufferResourceDesc);
  state.table.resize(count);
  if (!table_bytes.empty()) {
    std::memcpy(state.table.data(), table_bytes.data(), table_bytes.size());
  }

  state.appender.current_offset = std::filesystem::file_size(data_path, ec);
  if (ec) {
    throw std::runtime_error("Failed to get buffers.data file size");
  }

  LOG_F(INFO, "Loaded existing buffers: count={} data_size={}",
    state.table.size(), state.appender.current_offset);

  return state;
}

auto BuildTextureSignatureIndex(TextureEmissionState& state,
  [[maybe_unused]] const std::filesystem::path& data_path) -> void
{
  using util::MakeTextureSignatureFromStoredHash;

  state.index_by_signature.clear();
  state.index_by_signature.reserve(state.table.size());

  if (state.table.empty()) {
    return;
  }

  // Use stored content_hash - no data file read required
  for (uint32_t ti = 1; ti < state.table.size(); ++ti) {
    const auto& desc = state.table[ti];
    if (desc.size_bytes == 0) {
      continue;
    }

    const auto signature = MakeTextureSignatureFromStoredHash(desc);
    state.index_by_signature.emplace(signature, ti);
  }

  LOG_F(INFO, "Built texture signature index from stored hashes: {} entries",
    state.index_by_signature.size());
}

auto BuildBufferSignatureIndex(BufferEmissionState& state,
  [[maybe_unused]] const std::filesystem::path& data_path) -> void
{
  using util::MakeBufferSignatureFromStoredHash;

  state.index_by_signature.clear();
  state.index_by_signature.reserve(state.table.size());

  if (state.table.empty()) {
    return;
  }

  // Use stored content_hash - no data file read required
  for (uint32_t bi = 0; bi < state.table.size(); ++bi) {
    const auto& desc = state.table[bi];
    if (desc.size_bytes == 0) {
      continue;
    }

    const auto signature = MakeBufferSignatureFromStoredHash(desc);
    state.index_by_signature.emplace(signature, bi);
  }

  LOG_F(INFO, "Built buffer signature index from stored hashes: {} entries",
    state.index_by_signature.size());
}

} // namespace oxygen::content::import::emit
