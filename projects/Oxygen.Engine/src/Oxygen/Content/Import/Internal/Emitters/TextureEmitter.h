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
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/Internal/ResourceTableAggregator.h>
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
 TextureEmitter owns the `textures.data` file for a single import session
 and hands table entries to a shared `TextureTableAggregator`.
 It provides immediate index assignment with background async I/O for
 maximum throughput.

 ### PAK Compliance Notes

 - Texture resource index `data::pak::kFallbackResourceIndex` is reserved for
   the fallback texture.
 - The fallback entry is ensured on the first call to `Emit()` or
    `Finalize()`.

 ### Design Principles

 1. **Stable Index Immediately**: `Emit()` returns the table index
    synchronously. The index is valid the moment it's returned.

 2. **Async I/O in Background**: Data is written via `IAsyncFileWriter`
    without blocking the import thread.

 3. **Shared Table Aggregation**: Table entries are submitted to the
   `TextureTableAggregator`. The table file is written once during service
   shutdown.

 4. **Signature Dedupe**: Identical cooked textures are deduplicated using a
    stable signature derived from the cooked descriptor (and its stored hash).
    `Emit()` may return an existing index.

 ### Usage Pattern

 ```cpp
 TextureEmitter::Config config;
 config.cooked_root = cooked_root;
 config.layout = layout;
 config.packing_policy_id = "d3d12";

 TextureEmitter emitter(file_writer, table_aggregator, config);

 // During import (returns immediately)
 auto idx1 = emitter.Emit(cooked_texture1);  // Returns >= 1
 auto idx2 = emitter.Emit(cooked_texture2);  // Returns >= 1

 // After all cooking completes
 co_await emitter.Finalize();
 // textures.data is on disk; textures.table is written at service shutdown
 ```

 ### Thread Safety

 - `Emit()` must be called from the import thread only (not thread-safe).
 - `PendingCount()` and `ErrorCount()` are thread-safe (atomic reads).
 - `Finalize()` must be called from the import thread.

 @see IAsyncFileWriter, CookedTexturePayload
*/
class TextureEmitter final {
public:
  //! Configuration for texture emission.
  struct Config final {
    //! Root directory for cooked output files.
    std::filesystem::path cooked_root;

    //! Loose cooked layout describing output relative paths.
    LooseCookedLayout layout = {};

    //! Packing policy for fallback textures ("d3d12" or "tight").
    std::string packing_policy_id;

    //! Alignment for texture data placement in the data file.
    uint64_t data_alignment = 256;

    //! Enable or disable payload content hashing.
    /*!
     When false, fallback payloads MUST NOT compute `content_hash`.
    */
    bool with_content_hashing = true;
  };

  //! Runtime statistics for the emitter.
  struct Stats final {
    uint32_t emitted_textures = 0;
    uint64_t data_file_size = 0;
    size_t pending_writes = 0;
    size_t error_count = 0;
  };

  //! Create a texture emitter for the given layout.
  /*!
   @param file_writer Async file writer for output operations.
   @param table_aggregator Shared texture table aggregator.
   @param config Emission configuration.
  */
  OXGN_CNTT_API TextureEmitter(IAsyncFileWriter& file_writer,
    TextureTableAggregator& table_aggregator, Config config);

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
      @return Table index for this texture. Index
        `data::pak::kFallbackResourceIndex` is reserved for the fallback
        texture; user-emitted textures start at
        `data::pak::kFallbackResourceIndex + 1`.

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

  //! Get a snapshot of the current emitter statistics.
  OXGN_CNTT_NDAPI auto GetStats() const noexcept -> Stats;

  //=== Finalization
  //===-------------------------------------------------------//

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
  using TextureResourceDesc = data::pak::TextureResourceDesc;

  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  enum class TextureKind : uint8_t {
    kFallback,
    kUser,
  };

  //! Reserve an aligned range in the data file and return padding info.
  auto ReserveDataRange(uint64_t alignment, uint64_t payload_size)
    -> WriteReservation;

  //! Queue a write (padding or payload) to the data file.
  auto QueueDataWrite(WriteKind kind, TextureKind texture_kind,
    std::optional<uint32_t> index, uint64_t offset,
    std::shared_ptr<std::vector<std::byte>> data) -> void;

  //! Common completion handling for queued writes.
  auto OnWriteComplete(WriteKind kind, TextureKind texture_kind,
    std::optional<uint32_t> index, const FileErrorInfo& error) -> void;

  auto EnsureFallbackTexture() -> void;
  auto CreateFallbackPayload() const -> CookedTexturePayload;
  auto ToPakDescriptor(const CookedTexturePayload& cooked,
    uint64_t data_offset) const -> TextureResourceDesc;

  IAsyncFileWriter& file_writer_;
  TextureTableAggregator& table_aggregator_;
  Config config_ {};
  std::filesystem::path data_path_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<bool> fallback_emitted_ { false };
  std::atomic<uint32_t> emitted_count_ { 0 };
  std::atomic<uint64_t> data_file_size_ { 0 };
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
};

} // namespace oxygen::content::import
