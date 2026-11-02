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
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

//! Record of an emitted asset descriptor.
/*!
 Contains metadata about an asset written via AssetEmitter.
 Used for tracking and for eventual integration with LooseCookedWriter.
*/
struct EmittedAssetRecord {
  //! Stable identity of the asset.
  data::AssetKey key;

  //! Asset type for loader dispatch.
  data::AssetType asset_type = data::AssetType::kUnknown;

  //! Virtual path for tooling/editors (e.g., "/.cooked/Materials/Wood").
  std::string virtual_path;

  //! Container-relative path to descriptor file (e.g., "Materials/Wood.omat").
  std::string descriptor_relpath;

  //! Size of descriptor bytes written.
  uint64_t descriptor_size = 0;

  //! SHA-256 of the descriptor bytes, hashed as they were emitted.
  base::Sha256Digest descriptor_sha256 {};
  data::AssetReferences references;
};

//! Emits asset descriptors with async I/O.
/*!
 AssetEmitter writes individual asset descriptor files (`.omat`, `.ogeo`,
 `.oscene`) asynchronously. Unlike TextureEmitter/BufferEmitter which write
 to data/table file pairs, AssetEmitter writes each asset to its own file.

 ### Design Principles

 1. **Per-Asset Files**: Each `Emit()` writes a separate descriptor file
    (e.g., `Materials/Wood.omat`).

 2. **Async I/O**: Each descriptor is published through
    `ImportStorage::PublishAsync` by a task of the import session's nursery,
    without blocking the import thread. A publication replaces the file
    atomically, so a cancelled or failed import never leaves a torn
    descriptor.

 3. **No Index Assignment**: Unlike resource emitters, asset descriptors
    don't have numeric indices. Identity is via `AssetKey`.

 4. **Record Tracking**: Maintains list of emitted assets for eventual
    integration with `LooseCookedWriter::WriteAssetDescriptor()`.

 ### Usage Pattern

 ```cpp
 AssetEmitter emitter(storage, driver, session_nursery, cooked_root);

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

 - `Emit()` must be called from the import thread only, inside the coroutine
   scope of the nursery the emitter was given.
 - `Finalize()` must be called from the import thread.

 @see ImportStorage, LooseCookedWriter
*/
class AssetEmitter final {
public:
  //! Create an asset emitter writing under a cooked root.
  /*!
   @param storage     Where descriptors are published.
   @param driver      The driver of the import loop, which awaits the writes.
   @param writes      The nursery the writes run in; it outlives the emitter's
                      writes (the session's job nursery).
   @param cooked_root Root directory for cooked output.
  */
  OXGN_COOK_API AssetEmitter(cooked::ImportStorage& storage,
    serio::IoDriver& driver, co::Nursery& writes,
    std::filesystem::path cooked_root);

  OXGN_COOK_API ~AssetEmitter();

  OXYGEN_MAKE_NON_COPYABLE(AssetEmitter)
  OXYGEN_MAKE_NON_MOVABLE(AssetEmitter)

  //=== Emission ===----------------------------------------------------------//

  //! Emit an asset descriptor file.
  /*!
   Starts publishing the descriptor file in the nursery. The file path is
   determined by `descriptor_relpath` relative to the cooked root. A second
   emission of the same path while its write is in flight is queued: one
   write per path is in flight, and the latest bytes win. The emitted
   descriptor records its content hash in its header, whatever `bytes` holds
   there.

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

   I/O errors are tracked via `ErrorCount()` and returned by `Finalize()`.

   @throw std::runtime_error on invalid paths.
  */
  OXGN_COOK_API auto Emit(const data::AssetKey& key, data::AssetType asset_type,
    std::string_view virtual_path, std::string_view descriptor_relpath,
    std::span<const std::byte> bytes, data::AssetReferences references) -> void;

  //! Emit an asset descriptor file and wait for the write to complete.
  OXGN_COOK_NDAPI auto EmitSync(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes,
    data::AssetReferences references) -> co::Co<void>;

  //=== State Query
  //===-------------------------------------------------------//

  //! Get the number of assets emitted so far.
  OXGN_COOK_NDAPI auto Count() const noexcept -> size_t;

  //! Get the number of I/O errors encountered.
  OXGN_COOK_NDAPI auto ErrorCount() const noexcept -> size_t;

  //! Get the records of all emitted assets.
  OXGN_COOK_NDAPI auto Records() const noexcept
    -> const std::vector<EmittedAssetRecord>&;

  //=== Finalization ===------------------------------------------------------//

  //! Wait for all pending I/O to complete.
  /*!
   This method waits for the started writes to complete.

   @return Success or the first I/O error.

   ### Errors

   If any I/O errors occurred during `Emit()` calls, this method returns
   the first one; each is logged with its path as it happens.

   @note Must be called from the import thread.
  */
  OXGN_COOK_NDAPI auto Finalize() -> co::Co<Result<void, WriteFailure>>;

private:
  struct DescriptorWriteState {
    std::filesystem::path descriptor_path;
    bool write_in_flight = false;
    std::optional<std::vector<std::byte>> queued_bytes;
  };

  auto RecordAsset(const data::AssetKey& key, data::AssetType asset_type,
    std::string_view virtual_path, std::string_view descriptor_relpath,
    uint64_t descriptor_size, data::AssetReferences references,
    const base::Sha256Digest& sha256) -> void;

  auto QueueDescriptorWrite(const std::filesystem::path& descriptor_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void;

  //! Publishes `bytes`, then the bytes queued behind them, one at a time.
  auto WriteDescriptor(std::string descriptor_relpath,
    std::vector<std::byte> bytes) -> co::Co<Result<void>>;

  observer_ptr<cooked::ImportStorage> storage_;
  observer_ptr<serio::IoDriver> driver_;
  WriteGroup writes_;
  std::filesystem::path cooked_root_;

  std::atomic<bool> finalize_started_ { false };

  std::unordered_map<data::AssetKey, size_t> record_index_by_key_;
  std::unordered_map<std::string, data::AssetKey> key_by_virtual_path_;
  std::unordered_map<std::string, data::AssetKey> key_by_descriptor_relpath_;
  std::unordered_map<std::string, DescriptorWriteState> write_state_by_relpath_;

  std::vector<EmittedAssetRecord> records_;
};

} // namespace oxygen::content::import
