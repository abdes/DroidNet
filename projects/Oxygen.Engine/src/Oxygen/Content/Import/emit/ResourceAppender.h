//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Content/Import/util/TextureRepack.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::emit {

//! Manages append-only writes to a resource data file.
/*!
 This struct enables efficient incremental cooking by appending new
 resources without loading the entire existing data file into memory.

 ### Key Features

 - **Append-only I/O**: Opens data file in append mode
 - **Lazy initialization**: Stream only opened on first write
 - **Offset tracking**: Knows current EOF position without reading file

 ### Usage Pattern

 ```cpp
 ResourceAppender appender;
 appender.data_path = root / "textures.data";
 appender.current_offset = std::filesystem::file_size(appender.data_path);

 const auto offset = AppendResource(appender, bytes, alignment);
 ```
*/
struct ResourceAppender final {
  //! Path to the .data file.
  std::filesystem::path data_path;

  //! Current offset (EOF position) in the data file.
  uint64_t current_offset = 0;

  //! Lazily-opened output stream.
  mutable std::optional<std::ofstream> stream;
};

//! Appends bytes to a resource data file with alignment.
/*!
 Opens the stream lazily on first write. Pads to alignment boundary
 before writing.

 @param appender The ResourceAppender managing the file.
 @param bytes The bytes to append.
 @param alignment The alignment for the data.
 @return The offset where data was written.
*/
[[nodiscard]] auto AppendResource(ResourceAppender& appender,
  std::span<const std::byte> bytes, uint64_t alignment) -> uint64_t;

//! Flushes and closes the appender's stream.
auto CloseAppender(ResourceAppender& appender) -> void;

//! Reads the entire contents of a file into a byte vector.
/*!
 @param path The file path.
 @return The file contents, or nullopt on error.
*/
[[nodiscard]] auto TryReadWholeFileBytes(const std::filesystem::path& path)
  -> std::optional<std::vector<std::byte>>;

//! State for texture resource emission.
/*!
 Tracks existing and new texture resources during import.
*/
struct TextureEmissionState final {
  using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;

  //! Table of all texture descriptors (existing + new).
  std::vector<TextureResourceDesc> table;

  //! Maps ufbx_texture pointer to table index.
  std::unordered_map<const void*, uint32_t> index_by_file_texture;

  //! Maps normalized texture path/ID to table index.
  std::unordered_map<std::string, uint32_t> index_by_texture_id;

  //! Maps content signature to table index for deduplication.
  std::unordered_map<std::string, uint32_t> index_by_signature;

  //! Appender for the data file.
  ResourceAppender appender;
};

//! State for buffer resource emission.
/*!
 Tracks existing and new buffer resources during import.
*/
struct BufferEmissionState final {
  using BufferResourceDesc = oxygen::data::pak::BufferResourceDesc;

  //! Table of all buffer descriptors (existing + new).
  std::vector<BufferResourceDesc> table;

  //! Maps content signature to table index for deduplication.
  std::unordered_map<std::string, uint32_t> index_by_signature;

  //! Appender for the data file.
  ResourceAppender appender;
};

//! Initializes texture emission state from existing files.
/*!
 Loads the existing .table file (small) and gets the file size
 of the .data file without loading it into memory.

 @param table_path Path to textures.table.
 @param data_path Path to textures.data.
 @return Initialized emission state.
*/
[[nodiscard]] auto InitTextureEmissionState(
  const std::filesystem::path& table_path,
  const std::filesystem::path& data_path) -> TextureEmissionState;

//! Initializes buffer emission state from existing files.
/*!
 @param table_path Path to buffers.table.
 @param data_path Path to buffers.data.
 @return Initialized emission state.
*/
[[nodiscard]] auto InitBufferEmissionState(
  const std::filesystem::path& table_path,
  const std::filesystem::path& data_path) -> BufferEmissionState;

//! Builds signature index from existing table entries.
/*!
 This function needs access to the data file to compute signatures
 for existing resources. It reads only the portions needed.

 @param state The emission state with loaded table.
 @param data_path Path to the data file.
*/
auto BuildTextureSignatureIndex(
  TextureEmissionState& state, const std::filesystem::path& data_path) -> void;

//! Builds signature index from existing buffer table entries.
/*!
 @param state The emission state with loaded table.
 @param data_path Path to the data file.
*/
auto BuildBufferSignatureIndex(
  BufferEmissionState& state, const std::filesystem::path& data_path) -> void;

} // namespace oxygen::content::import::emit
