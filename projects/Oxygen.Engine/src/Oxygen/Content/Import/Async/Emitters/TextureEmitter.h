//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;
struct FileErrorInfo;

//! Emits cooked textures with async I/O.
/*!
 TextureEmitter owns the `textures.data` and `textures.table` files for
 a single import session. It provides immediate index assignment with
 background async I/O for maximum throughput.

 ### PAK Compliance Notes

 - Texture resource index `0` is reserved for the fallback texture.
 - The fallback entry is ensured on the first call to `Emit()` or
    `Finalize()`.

 ### Design Principles

 1. **Stable Index Immediately**: `Emit()` returns the table index
    synchronously. The index is valid the moment it's returned.

 2. **Async I/O in Background**: Data is written via `IAsyncFileWriter`
    without blocking the import thread.

 3. **In-Memory Table**: Table entries are accumulated in memory (~100 bytes
    each). The table file is written once during `Finalize()`.

 4. **Signature Dedupe**: Identical cooked textures are deduplicated using a
    stable signature derived from the cooked descriptor (and its stored hash).
    `Emit()` may return an existing index.

 ### Usage Pattern

 ```cpp
 TextureEmitter emitter(file_writer, layout, cooked_root);

 // During import (returns immediately)
 auto idx1 = emitter.Emit(cooked_texture1);  // Returns >= 1
 auto idx2 = emitter.Emit(cooked_texture2);  // Returns >= 1

 // After all cooking completes
 co_await emitter.Finalize();
 // textures.data and textures.table are now on disk
 ```

 ### Thread Safety

 - `Emit()` must be called from the import thread only (not thread-safe).
 - `PendingCount()` and `ErrorCount()` are thread-safe (atomic reads).
 - `Finalize()` must be called from the import thread.

 @see IAsyncFileWriter, CookedTexturePayload
*/
class TextureEmitter final {
public:
  //! Create a texture emitter for the given layout.
  /*!
   @param file_writer Async file writer for output operations.
   @param layout      Loose cooked layout defining paths.
   @param cooked_root Root directory for cooked output.
  */
  OXGN_CNTT_API TextureEmitter(IAsyncFileWriter& file_writer,
    const LooseCookedLayout& layout, const std::filesystem::path& cooked_root);

  OXGN_CNTT_API ~TextureEmitter();

  OXYGEN_MAKE_NON_COPYABLE(TextureEmitter)
  OXYGEN_MAKE_NON_MOVABLE(TextureEmitter)

  //=== Emission
  //===----------------------------------------------------------//

  //! Emit a cooked texture, returning a stable table index.
  /*!
   Assigns a table index immediately and queues an async write for the
   texture data. The index is valid the moment this method returns.

   @param cooked The cooked texture payload containing descriptor and data.
      @return Table index for this texture. Index 0 is reserved for the fallback
         texture; user-emitted textures start at 1.

   ### What Happens

   1. Assigns the next index via atomic increment.
   2. Creates a `TextureResourceDesc` with data offset (based on current
      data file position).
   3. Queues async writes at explicit offsets to `textures.data`.
   4. Adds the descriptor to the in-memory table vector.
   5. Returns the index.

   ### Error Handling

   I/O errors are tracked via `ErrorCount()`. The caller should check
   this during finalization.

   @note The returned index is stable even if the I/O fails later.
  */
  OXGN_CNTT_NDAPI auto Emit(CookedTexturePayload cooked) -> uint32_t;

  //=== State Query
  //===-------------------------------------------------------//

  //! Get the number of textures emitted so far.
  OXGN_CNTT_NDAPI auto Count() const noexcept -> uint32_t;

  //! Get the number of pending async write operations.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get the number of I/O errors encountered.
  OXGN_CNTT_NDAPI auto ErrorCount() const noexcept -> size_t;

  //! Get the current reserved data size.
  OXGN_CNTT_NDAPI auto DataFileSize() const noexcept -> uint64_t;

  //=== Finalization
  //===-------------------------------------------------------//

  //! Wait for all pending I/O and write the table file.
  /*!
   This method:
   1. Waits for all pending async writes to complete.
   2. Writes the in-memory table to `textures.table`.

   @return True if all writes succeeded, false if any errors occurred.

   ### Errors

   If any I/O errors occurred during `Emit()` calls, this method returns
   false. The caller should check `ErrorCount()` for details.

   @note Must be called from the import thread.
  */
  OXGN_CNTT_NDAPI auto Finalize() -> co::Co<bool>;

private:
  using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;

  struct ReservedWriteRange {
    uint64_t reservation_start = 0;
    uint64_t aligned_offset = 0;
    uint64_t padding_size = 0;
  };

  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  enum class TextureKind : uint8_t {
    kFallback,
    kUser,
  };

  //! Build a table descriptor from a cooked payload.
  auto MakeTableEntry(const CookedTexturePayload& cooked, uint64_t data_offset)
    -> TextureResourceDesc;

  //! Find an existing index for the signature, if present.
  auto FindExistingIndex(const std::string& signature) const
    -> std::optional<uint32_t>;

  //! Reserve an aligned range in the data file and return padding info.
  auto ReserveDataRange(uint64_t alignment, uint64_t payload_size)
    -> ReservedWriteRange;

  //! Record a newly emitted texture in the table and dedupe map.
  auto RecordTextureEntry(const std::string& signature, uint32_t index,
    const CookedTexturePayload& cooked, uint64_t aligned_offset) -> void;

  //! Record the fallback texture entry.
  auto RecordFallbackEntry(
    const std::string& signature, const TextureResourceDesc& desc) -> void;

  //! Queue a write (padding or payload) to the data file.
  auto QueueDataWrite(WriteKind kind, TextureKind texture_kind,
    std::optional<uint32_t> index, uint64_t offset,
    std::shared_ptr<std::vector<std::byte>> data) -> void;

  //! Common completion handling for queued writes.
  auto OnWriteComplete(WriteKind kind, TextureKind texture_kind,
    std::optional<uint32_t> index, const FileErrorInfo& error) -> void;

  auto EnsureFallbackTexture() -> void;

  //! Write the table file to disk.
  auto WriteTableFile() -> co::Co<bool>;

  IAsyncFileWriter& file_writer_;
  std::filesystem::path data_path_;
  std::filesystem::path table_path_;

  std::vector<TextureResourceDesc> table_;
  std::unordered_map<std::string, uint32_t> index_by_signature_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<uint32_t> next_index_ { 0 };
  std::atomic<uint64_t> data_file_size_ { 0 };
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
};

} // namespace oxygen::content::import
