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
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>

namespace oxygen::content::import {

//! Emits cooked buffers with async I/O.
/*!
 BufferEmitter owns the `buffers.data` file for a single import session and
 submits table entries to a shared `BufferTableAggregator`.
 It provides immediate index assignment with background async I/O for
 maximum throughput.

 ### Design Principles

 1. **Stable Index Immediately**: `Emit()` returns the table index
    synchronously. The index is valid the moment it's returned.

 2. **Async I/O in Background**: Data is written at its reserved offset to
    the data file the aggregator opened once, by tasks of the import
    session's nursery, without blocking the import thread.

 3. **Shared Table Aggregation**: Table entries are submitted to the
   `BufferTableAggregator`. The last import session of the cooked root's
   cohort writes the table when it ends.

 4. **Signature-Based Deduplication**: A buffer whose row matches an existing
    row in every field but its offset, SHA-256 included, reuses that row
    when the row's offset satisfies the buffer's alignment, without any
    additional I/O.

 5. **Per-Buffer Alignment**: Each buffer specifies its own alignment
    requirement (vertex buffers = 16 bytes, index buffers = 4 bytes, etc.).
    Padding is written between buffers to maintain alignment.

 ### PAK Format Compliance

 - Uses `BufferResourceDesc` from `PakFormat.h`
 - Alignment is per-buffer (specified in `CookedBufferPayload::alignment`)
 - Table is serialized with packed alignment (no padding between entries)

 ### Usage Pattern

 ```cpp
 BufferEmitter emitter(session_nursery, table_aggregator, cooked_root);

 // During import (returns immediately); index 0 is the absent sentinel
 auto vb_idx = emitter.Emit(vertex_buffer_payload);  // Returns 1
 auto ib_idx = emitter.Emit(index_buffer_payload);   // Returns 2

 // After all cooking completes
 co_await emitter.Finalize();
 // buffers.data is on disk; buffers.table is written when the session ends
 ```

 ### Thread Safety

 - `Emit()` must be called from the import thread only (not thread-safe),
   inside the coroutine scope of the nursery the emitter was given.
 - `Finalize()` must be called from the import thread.

 @see CookedBufferPayload, BufferResourceDesc
*/
class BufferEmitter final {
public:
  //! Create a buffer emitter writing under a cooked root.
  /*!
   @param writes The nursery the writes run in; it outlives them (the
     session's job nursery).
   @param table_aggregator Shared buffer table aggregator; it holds the open
     data file.
   @param cooked_root Root directory for cooked output.
  */
  OXGN_COOK_API BufferEmitter(co::Nursery& writes,
    BufferTableAggregator& table_aggregator,
    const std::filesystem::path& cooked_root);

  OXGN_COOK_API ~BufferEmitter();

  OXYGEN_MAKE_NON_COPYABLE(BufferEmitter)
  OXYGEN_MAKE_NON_MOVABLE(BufferEmitter)

  //=== Emission ===----------------------------------------------------------//

  //! Emit a cooked buffer, returning a stable table index.
  /*!
   Assigns a table index immediately and starts an async write for the
   buffer data. The index is valid the moment this method returns.

   @param cooked The cooked buffer payload containing data and metadata.
   @return Table index for this buffer. When deduplicated, this may be an
     existing index.
   @throw std::invalid_argument When the payload is empty.

   ### What Happens

  1. Builds the buffer's row from the payload metadata and its SHA-256.
  2. If an equal row with a suitably aligned offset exists, returns its index.
  3. Otherwise assigns the next index at an offset aligned to the buffer's
     alignment.
  4. Starts async writes at explicit offsets (padding, then buffer bytes)
     to `buffers.data`.
  5. Returns the assigned index.

   ### Error Handling

   I/O errors are tracked via `ErrorCount()` and returned by `Finalize()`.

   @note The returned index is stable even if the I/O fails later.
  */
  OXGN_COOK_NDAPI auto Emit(CookedBufferPayload cooked) -> uint32_t;

  //=== State Query ===-------------------------------------------------------//

  //! Get the number of buffers emitted so far.
  OXGN_COOK_NDAPI auto Count() const noexcept -> uint32_t;

  //! Get the number of I/O errors encountered.
  OXGN_COOK_NDAPI auto ErrorCount() const noexcept -> size_t;

  //! Get the current reserved data size.
  OXGN_COOK_NDAPI auto DataFileSize() const noexcept -> uint64_t;

  //! Query the table descriptor for a previously emitted buffer index.
  OXGN_COOK_NDAPI auto TryGetDescriptor(uint32_t index) const
    -> std::optional<data::pak::core::BufferResourceDesc>;

  //=== Finalization ===------------------------------------------------------//

  //! Wait for all pending I/O for this session.
  /*!
   This method:
   1. Waits for all pending async writes to complete.

   @return Success or the first I/O error.

   ### Errors

   If any I/O errors occurred during `Emit()` calls, this method returns
   the first one; each is logged with its path as it happens.

   @note Must be called from the import thread.
  */
  OXGN_COOK_NDAPI auto Finalize() -> co::Co<Result<void, WriteFailure>>;

private:
  using BufferResourceDesc = data::pak::core::BufferResourceDesc;

  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  //! Build the table row of a cooked payload, without its data offset.
  static auto MakeTableEntry(const CookedBufferPayload& cooked)
    -> BufferResourceDesc;

  //! Start a write (padding or payload) to the data file.
  auto QueueDataWrite(WriteKind kind, std::optional<uint32_t> index,
    uint64_t offset, std::vector<std::byte> data) -> void;

  //! The write task: awaits the data at its offset and logs a failure.
  auto WriteData(serio::AnyAsyncDevice* file, WriteKind kind,
    std::optional<uint32_t> index, uint64_t offset, std::vector<std::byte> data)
    -> co::Co<Result<void>>;

  WriteGroup writes_;
  BufferTableAggregator& table_aggregator_;
  std::filesystem::path data_path_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<uint32_t> emitted_count_ { 0 };
};

} // namespace oxygen::content::import
