//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data::loose_cooked {

//! An asset or a texture of another mount that an asset references.
struct IndexedRequirement final {
  //! The required mount's name, such as `Engine`.
  std::string mount;
  //! `kAsset` or `kTexture`.
  KeyReferenceKind kind = KeyReferenceKind::kAsset;
  AssetKey key;
  //! The required asset's type; `kUnknown` for a texture.
  AssetType type = AssetType::kUnknown;

  auto operator==(const IndexedRequirement&) const -> bool = default;
};

//! An asset a loose cooked index lists: its descriptor file and identity.
struct IndexedAsset final {
  AssetKey key;
  AssetType asset_type = AssetType::kUnknown;
  //! Canonical virtual path, such as `/Content/Materials/Dark.omat`.
  std::string virtual_path;
  //! Container-relative path of the descriptor file.
  std::string descriptor_relpath;

  uint64_t descriptor_size = 0;
  base::Sha256Digest descriptor_sha256 {};
  AssetReferences references;
  //! Virtual path of the source that produced the asset.
  std::string source_path;
  //! What the asset requires of other mounts, each once.
  std::vector<IndexedRequirement> requirements;
};

//! A file a loose cooked index lists other than an asset descriptor: a
//! resource table or data file, a texture descriptor or an auxiliary file.
struct IndexedFile final {
  FileKind kind = FileKind::kUnknown;
  //! Container-relative path of the file.
  std::string relpath;

  uint64_t size = 0;
  base::Sha256Digest sha256 {};
  //! The virtual path a reference names a texture descriptor or an
  //! auxiliary file by; empty for every other file.
  std::string virtual_path;
  //! Mount path of the source that produced a texture descriptor or an
  //! auxiliary file; empty for every other file.
  std::string source_path;
};

//! A file an earlier commit listed and this one does not, kept until no
//! reader of those commits remains.
struct RetiredFile final {
  FileKind kind = FileKind::kUnknown;
  //! Container-relative path of the file.
  std::string relpath;
  //! The first commit that does not list it.
  uint64_t retired_at = 0;

  auto operator==(const RetiredFile&) const -> bool = default;
};

//! What a loose cooked index records.
struct IndexContent final {
  SourceKey source_key {};
  //! Cook-defined content version of the index header.
  uint16_t content_version = 0;
  //! The commit the index is, from 1.
  uint64_t commit = 1;
  std::span<const IndexedAsset> assets;
  std::span<const IndexedFile> files;
  std::span<const RetiredFile> retired;
};

//! Streams `content` through `writer` as a `container.index.bin`.
/*!
 The index is written in one pass, in the order of its sections, into
 `writer`: a publication or a memory stream receives it as it is encoded. The
 strings are written straight from `content`, never copied; the fixed-size
 asset entries and file records are built in memory first, since their string
 offsets are known only once the string table is laid out. Assets are written
 in key order and files in path order, so equal content always encodes to
 equal bytes, whatever the order of its spans.

 ```cpp
 auto stream = serio::MemoryStream {};
 auto writer = serio::Writer(stream);
 CHECK_RESULT(EncodeLooseCookedIndex(content, writer));
 ```

 Path rules (canonical virtual paths, container-relative paths) are the
 producer's; the loader refuses an index that breaks them.

 @return The first error of the stream; the bytes written before it are not
   an index.
 @throw std::invalid_argument for content the loader would refuse, so a
   producer, or a test, fails where it builds the content: an asset key, a
   virtual path, a container-relative path or a table or data file kind
   listed twice; a path `RequireIndexRelPath` refuses; an asset without a
   virtual path or producing source; a texture descriptor without a virtual
   path or producing source; a file other than an auxiliary file with a
   virtual path or a producing source; a missing digest; a
   commit of 0; a retired file listed twice, also listed, or retired at no
   commit from 1 to `commit`; a requirement of no mount, of a kind other than
   an asset or a texture, of a texture with a type, or listed twice for one
   asset; or a string table past 4 GiB. Canonical virtual path spelling is
   the producer's, and the loader checks it.
*/
OXGN_DATA_NDAPI auto EncodeLooseCookedIndex(
  const IndexContent& content, serio::AnyWriter& writer) -> Result<void>;

} // namespace oxygen::data::loose_cooked
