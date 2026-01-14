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
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/Async/FileError.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;

//! Record of an emitted asset descriptor.
/*!
 Contains metadata about an asset written via AssetEmitter.
 Used for tracking and for eventual integration with LooseCookedWriter.
*/
struct EmittedAssetRecord {
  //! Stable identity of the asset.
  data::AssetKey key {};

  //! Asset type for loader dispatch.
  data::AssetType asset_type = data::AssetType::kUnknown;

  //! Virtual path for tooling/editors (e.g., "/.cooked/Materials/Wood").
  std::string virtual_path;

  //! Container-relative path to descriptor file (e.g., "Materials/Wood.omat").
  std::string descriptor_relpath;

  //! Size of descriptor bytes written.
  uint64_t descriptor_size = 0;

  //! SHA-256 hash of the descriptor bytes (for index validation).
  std::optional<base::Sha256Digest> descriptor_sha256;
};

//! Emits asset descriptors with async I/O.
/*!
 AssetEmitter writes individual asset descriptor files (`.omat`, `.ogeo`,
 `.oscene`) asynchronously. Unlike TextureEmitter/BufferEmitter which write
 to data/table file pairs, AssetEmitter writes each asset to its own file.

 ### Design Principles

 1. **Per-Asset Files**: Each `Emit()` writes a separate descriptor file
    (e.g., `Materials/Wood.omat`).

 2. **Async I/O**: Writes are queued via `IAsyncFileWriter` without blocking
    the import thread.

 3. **No Index Assignment**: Unlike resource emitters, asset descriptors
    don't have numeric indices. Identity is via `AssetKey`.

 4. **Record Tracking**: Maintains list of emitted assets for eventual
    integration with `LooseCookedWriter::WriteAssetDescriptor()`.

 ### Usage Pattern

 ```cpp
 AssetEmitter emitter(file_writer, layout, cooked_root);

 // During import
 emitter.Emit(material_key, AssetType::kMaterial,
              "/.cooked/Materials/Wood", "Materials/Wood.omat",
              material_bytes);
 emitter.Emit(geometry_key, AssetType::kGeometry,
              "/.cooked/Geometry/Cube", "Geometry/Cube.ogeo",
              geometry_bytes);

 // After all cooking completes
 co_await emitter.Finalize();
 // All .omat, .ogeo, .oscene files are now on disk
 ```

 ### Thread Safety

 - `Emit()` must be called from the import thread only.
 - `PendingCount()` and `ErrorCount()` are thread-safe (atomic reads).
 - `Finalize()` must be called from the import thread.

 @see IAsyncFileWriter, LooseCookedWriter
*/
class AssetEmitter final {
public:
  //! Create an asset emitter for the given layout.
  /*!
   @param file_writer Async file writer for output operations.
   @param layout      Loose cooked layout defining paths.
   @param cooked_root Root directory for cooked output.
   @param compute_sha256 Whether to compute SHA-256 hashes for records.
  */
  OXGN_CNTT_API AssetEmitter(IAsyncFileWriter& file_writer,
    const LooseCookedLayout& layout, const std::filesystem::path& cooked_root,
    bool compute_sha256 = true);

  OXGN_CNTT_API ~AssetEmitter();

  OXYGEN_MAKE_NON_COPYABLE(AssetEmitter)
  OXYGEN_MAKE_NON_MOVABLE(AssetEmitter)

  //=== Emission ===----------------------------------------------------------//

  //! Emit an asset descriptor file.
  /*!
   Queues an async write for the descriptor file. The file path is
   determined by `descriptor_relpath` relative to the cooked root.

   @param key              Stable identity of the asset.
   @param asset_type       Asset type for loader dispatch.
   @param virtual_path     Virtual path for tooling/editors.
   @param descriptor_relpath Container-relative path for descriptor file.
   @param bytes            Serialized descriptor bytes.

   ### Path Validation

   Both paths are validated to match PAK format requirements:
   - `virtual_path`: Must start with `/`, use `/` separators, no `//`, `.`, `..`
   - `descriptor_relpath`: Must be container-relative, use `/` separators,
     no `:`, no leading `/`, no `//`, `.`, `..`

   ### Error Handling

   I/O errors are tracked via `ErrorCount()`. The caller should check
   this during finalization.

   @throw std::runtime_error on invalid paths.
  */
  OXGN_CNTT_API auto Emit(const data::AssetKey& key, data::AssetType asset_type,
    std::string_view virtual_path, std::string_view descriptor_relpath,
    std::span<const std::byte> bytes) -> void;

  //=== State Query
  //===-------------------------------------------------------//

  //! Get the number of assets emitted so far.
  OXGN_CNTT_NDAPI auto Count() const noexcept -> size_t;

  //! Get the number of pending async write operations.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get the number of I/O errors encountered.
  OXGN_CNTT_NDAPI auto ErrorCount() const noexcept -> size_t;

  //! Get the records of all emitted assets.
  OXGN_CNTT_NDAPI auto Records() const noexcept
    -> const std::vector<EmittedAssetRecord>&;

  //=== Finalization ===------------------------------------------------------//

  //! Wait for all pending I/O to complete.
  /*!
   This method waits for all pending async writes to complete.

   @return True if all writes succeeded, false if any errors occurred.

   ### Errors

   If any I/O errors occurred during `Emit()` calls, this method returns
   false. The caller should check `ErrorCount()` for details.

   @note Must be called from the import thread.
  */
  OXGN_CNTT_NDAPI auto Finalize() -> co::Co<bool>;

private:
  struct DescriptorWriteState {
    std::filesystem::path descriptor_path;
    bool write_in_flight = false;
    std::shared_ptr<std::vector<std::byte>> queued_bytes;
  };

  auto RecordAsset(const data::AssetKey& key, data::AssetType asset_type,
    std::string_view virtual_path, std::string_view descriptor_relpath,
    uint64_t descriptor_size, std::optional<base::Sha256Digest> sha256) -> void;

  auto QueueDescriptorWrite(const std::filesystem::path& descriptor_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void;

  auto OnWriteComplete(
    std::string_view descriptor_relpath, const FileErrorInfo& error) -> void;

  IAsyncFileWriter& file_writer_;
  std::filesystem::path cooked_root_;

  bool compute_sha256_ = true;

  std::atomic<bool> finalize_started_ { false };

  std::unordered_map<data::AssetKey, size_t> record_index_by_key_;
  std::unordered_map<std::string, data::AssetKey> key_by_virtual_path_;
  std::unordered_map<std::string, DescriptorWriteState> write_state_by_relpath_;

  std::vector<EmittedAssetRecord> records_;
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
};

} // namespace oxygen::content::import
