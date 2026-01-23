//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! Summary of one cooked asset descriptor written to disk.
struct LooseCookedAssetRecord final {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::string virtual_path;
  std::string descriptor_relpath;

  uint64_t descriptor_size = 0;
  std::optional<base::Sha256Digest> descriptor_sha256;
};

//! Summary of one cooked file record written to disk.
struct LooseCookedFileRecord final {
  data::loose_cooked::v1::FileKind kind
    = data::loose_cooked::v1::FileKind::kUnknown;
  std::string relpath;

  uint64_t size = 0;
};

//! Result of writing a loose cooked container.
struct LooseCookedWriteResult final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key { std::array<uint8_t, 16> {} };

  uint16_t content_version = 0;
  std::vector<LooseCookedAssetRecord> assets;
  std::vector<LooseCookedFileRecord> files;
};

//! Write a loose cooked container root.
/*!
 This is a small, tooling-oriented helper responsible for:
 - writing asset descriptor files,
 - writing optional resource table/data files,
 - emitting a valid `container.index.bin` matching
   oxygen::data::loose_cooked::v1.

 It is designed to be used by importers (FBX/glTF) and other cook pipelines.

 @note The on-disk schema is fixed by
   oxygen::data::loose_cooked::v1::IndexHeader and related structs.
*/
class LooseCookedWriter final {
public:
  //! Create a writer targeting `cooked_root`.
  OXGN_CNTT_API explicit LooseCookedWriter(std::filesystem::path cooked_root);
  OXGN_CNTT_API ~LooseCookedWriter();

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedWriter)
  OXYGEN_DEFAULT_MOVABLE(LooseCookedWriter)

  //! Set the source GUID recorded in the index.
  /*!
   If unset, the implementation may generate a new GUID.
  */
  OXGN_CNTT_API auto SetSourceKey(std::optional<data::SourceKey> key) -> void;

  //! Set the cook-defined content version recorded in the index header.
  OXGN_CNTT_API auto SetContentVersion(uint16_t version) -> void;

  //! Enable or disable SHA-256 hashing in emitted file records.
  /*!
   When disabled, the writer emits all-zero hashes.

   @note Runtime validation policy may still check size and existence.
  */
  OXGN_CNTT_API auto SetComputeSha256(bool enabled) -> void;

  //! Write an asset descriptor and update its index entry.
  /*!
   @param key Stable identity of the asset.
   @param asset_type Loader dispatch type.
   @param virtual_path Virtual path used by tooling/editors.
   @param descriptor_relpath Container-relative path for the descriptor file.
   @param bytes Descriptor bytes; must match the runtime loader schema.

   ### Update Semantics

   If the cooked root already contains an index file, the writer MUST treat the
   operation as an update:

   - Existing index entries MUST be preserved unless explicitly replaced.
   - An asset is identified by its `key`. If an entry for `key` already exists,
     the writer MUST update that entry (descriptor relpath, virtual path,
     size/hash) rather than adding a duplicate.

   @warning If updating an entry would create a conflicting virtual path
    mapping, the writer MUST fail with a diagnostic (or throw).

   @throw std::runtime_error on IO errors or invalid paths.
  */
  OXGN_CNTT_API auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void;

  //! Write an arbitrary file and update its index record.
  /*!
   This is primarily intended for resource table/data files referenced by the
   runtime (buffers/textures).

   @param kind Logical kind of file (see LooseCookedIndexFormat.h).
   @param relpath Container-relative path.
   @param bytes File bytes.

   ### Update Semantics

   If the cooked root already contains an index file, the writer MUST treat the
   operation as an update:

   - Existing file records MUST be preserved unless explicitly replaced.
   - A file record is identified by its `kind`. If a record for `kind` already
     exists, the writer MUST update that record (relpath, size/hash) rather
     than adding a duplicate.

   @throw std::runtime_error on IO errors or invalid paths.
  */
  OXGN_CNTT_API auto WriteFile(data::loose_cooked::v1::FileKind kind,
    std::string_view relpath, std::span<const std::byte> bytes) -> void;

  //! Register an externally-written file.
  /*!
   This is used when the data file was written directly (e.g., by
   append-only ResourceAppender) rather than through WriteFile().
   The file must already exist on disk at the given relpath.

   @param kind The file kind to register.
   @param relpath Container-relative file path.
   @throw std::runtime_error if the file does not exist.
  */
  OXGN_CNTT_API auto RegisterExternalFile(
    data::loose_cooked::v1::FileKind kind, std::string_view relpath) -> void;

  //! Register an externally-written asset descriptor.
  /*!
   This is used when an asset descriptor file was written by an external
   component (for example, an async emitter) rather than through
   WriteAssetDescriptor(). The file must already exist on disk at the given
   relpath.

   @param key Stable identity of the asset.
   @param asset_type Loader dispatch type.
   @param virtual_path Virtual path used by tooling/editors.
   @param descriptor_relpath Container-relative path for the descriptor file.
   @param descriptor_size Size of the descriptor bytes. If zero, the size will
     be read from disk.
   @param descriptor_sha256 Optional SHA-256 digest of the descriptor bytes.
     When `SetComputeSha256(false)` was selected, the writer records an
     all-zero hash.

   @throw std::runtime_error if the file does not exist, paths are invalid, or
     metadata is inconsistent.
  */
  OXGN_CNTT_API auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    std::optional<base::Sha256Digest> descriptor_sha256 = std::nullopt) -> void;

  //! Finalize and write the loose cooked index.
  /*!
   @return Summary of written assets and file records.

   The writer MUST not discard an existing index. If an index file already
   exists, `Finish()` MUST merge new/updated entries into it.

   @throw std::runtime_error if required metadata is missing or index emission
     fails.
  */
  OXGN_CNTT_NDAPI auto Finish() -> LooseCookedWriteResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
