//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/BufferImportTypes.h>
#include <Oxygen/Content/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;
struct FileErrorInfo;

//! Emits cooked buffers with async I/O.
/*!
 BufferEmitter owns the `buffers.data` file for a single import session and
 submits table entries to a shared `BufferTableAggregator`.
 It provides immediate index assignment with background async I/O for
 maximum throughput.

 ### Design Principles

 1. **Stable Index Immediately**: `Emit()` returns the table index
    synchronously. The index is valid the moment it's returned.

 2. **Async I/O in Background**: Data is written via `IAsyncFileWriter`
    without blocking the import thread.

 3. **Shared Table Aggregation**: Table entries are submitted to the
   `BufferTableAggregator`. The table file is written once during service
   shutdown.

 4. **Signature-Based Deduplication**: Buffers are deduplicated using a
   signature derived from the cooked payload metadata (usage/format/stride,
   alignment, and byte size). When `CookedBufferPayload::content_hash` is
   present (non-zero), it is incorporated as well. Duplicate buffers reuse an
   existing index without any additional I/O.

   @note When content hashing is disabled (hash is zero), the signature does
       not include content bytes and may deduplicate different buffers that
       share identical metadata and size.

 5. **Per-Buffer Alignment**: Each buffer specifies its own alignment
    requirement (vertex buffers = 16 bytes, index buffers = 4 bytes, etc.).
    Padding is written between buffers to maintain alignment.

 ### PAK Format Compliance

 - Uses `BufferResourceDesc` (32 bytes packed) from `PakFormat.h`
 - Alignment is per-buffer (specified in `CookedBufferPayload::alignment`)
 - Table is serialized with packed alignment (no padding between entries)

 ### Usage Pattern

 ```cpp
 BufferEmitter emitter(file_writer, layout, cooked_root);

 // During import (returns immediately)
 auto vb_idx = emitter.Emit(vertex_buffer_payload);  // Returns 0
 auto ib_idx = emitter.Emit(index_buffer_payload);   // Returns 1

 // After all cooking completes
 co_await emitter.Finalize();
 // buffers.data is on disk; buffers.table is written at service shutdown
 ```

 ### Thread Safety

 - `Emit()` must be called from the import thread only (not thread-safe).
 - `PendingCount()` and `ErrorCount()` are thread-safe (atomic reads).
 - `Finalize()` must be called from the import thread.

 @see IAsyncFileWriter, CookedBufferPayload, BufferResourceDesc
*/
class BufferEmitter final {
public:
  //! Create a buffer emitter for the given layout.
  /*!
   @param file_writer Async file writer for output operations.
   @param layout      Loose cooked layout defining paths.
   @param cooked_root Root directory for cooked output.
  */
  OXGN_CNTT_API BufferEmitter(IAsyncFileWriter& file_writer,
    BufferTableAggregator& table_aggregator, const LooseCookedLayout& layout,
    const std::filesystem::path& cooked_root);

  OXGN_CNTT_API ~BufferEmitter();

  OXYGEN_MAKE_NON_COPYABLE(BufferEmitter)
  OXYGEN_MAKE_NON_MOVABLE(BufferEmitter)

  //=== Emission ===----------------------------------------------------------//

  //! Emit a cooked buffer, returning a stable table index.
  /*!
   Assigns a table index immediately and queues an async write for the
   buffer data. The index is valid the moment this method returns.

   @param cooked The cooked buffer payload containing data and metadata.
     @return Table index for this buffer (0-based). When deduplicated, this may
       be an existing index.

   ### What Happens

  1. Builds a dedupe signature from the payload metadata (and optional hash).
  2. If the signature was seen before, returns the existing index.
  3. Otherwise assigns the next index.
  4. Calculates an aligned data offset based on the buffer's alignment.
  5. Queues async writes at explicit offsets (padding, then buffer bytes)
     to `buffers.data`.
  6. Adds the descriptor to the in-memory table and records the signature.
  7. Returns the assigned index.

   ### Error Handling

   I/O errors are tracked via `ErrorCount()`. The caller should check
   this during finalization.

   @note The returned index is stable even if the I/O fails later.
  */
  OXGN_CNTT_NDAPI auto Emit(CookedBufferPayload cooked) -> uint32_t;

  //=== State Query ===-------------------------------------------------------//

  //! Get the number of buffers emitted so far.
  OXGN_CNTT_NDAPI auto Count() const noexcept -> uint32_t;

  //! Get the number of pending async write operations.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get the number of I/O errors encountered.
  OXGN_CNTT_NDAPI auto ErrorCount() const noexcept -> size_t;

  //! Get the current reserved data size.
  OXGN_CNTT_NDAPI auto DataFileSize() const noexcept -> uint64_t;

  //=== Finalization ===------------------------------------------------------//

  //! Wait for all pending I/O for this session.
  /*!
   This method:
   1. Waits for all pending async writes to complete.

   @return True if all writes succeeded, false if any errors occurred.

   ### Errors

   If any I/O errors occurred during `Emit()` calls, this method returns
   false. The caller should check `ErrorCount()` for details.

   @note Must be called from the import thread.
  */
  OXGN_CNTT_NDAPI auto Finalize() -> co::Co<bool>;

private:
  using BufferResourceDesc = data::pak::BufferResourceDesc;

  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  //! Build a table descriptor from a cooked payload.
  auto MakeTableEntry(const CookedBufferPayload& cooked, uint64_t data_offset)
    -> BufferResourceDesc;

  //! Queue a write (padding or payload) to the data file.
  auto QueueDataWrite(WriteKind kind, std::optional<uint32_t> index,
    uint64_t offset, std::shared_ptr<std::vector<std::byte>> data) -> void;

  //! Common completion handling for queued writes.
  auto OnWriteComplete(WriteKind kind, std::optional<uint32_t> index,
    const FileErrorInfo& error) -> void;

  IAsyncFileWriter& file_writer_;
  BufferTableAggregator& table_aggregator_;
  std::filesystem::path data_path_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<uint32_t> emitted_count_ { 0 };
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
};

} // namespace oxygen::content::import
