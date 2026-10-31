//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>

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
};

//! A file a loose cooked index lists other than an asset descriptor: a
//! resource table or data file, a texture descriptor or an auxiliary file.
struct IndexedFile final {
  FileKind kind = FileKind::kUnknown;
  //! Container-relative path of the file.
  std::string relpath;

  uint64_t size = 0;
  base::Sha256Digest sha256 {};
  //! Virtual path of a texture descriptor; empty for every other file.
  std::string virtual_path;
  //! Mount path of the source that produced a texture descriptor or an
  //! auxiliary file; empty for every other file.
  std::string source_path;
};

//! What a loose cooked index records.
struct IndexContent final {
  SourceKey source_key {};
  //! Cook-defined content version of the index header.
  uint16_t content_version = 0;
  std::span<const IndexedAsset> assets;
  std::span<const IndexedFile> files;
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
   path or producing source; another file with a virtual path, or with a
   producing source unless it is an auxiliary file; a missing digest; or a
   string table past 4 GiB. Canonical virtual path spelling is the
   producer's, and the loader checks it.
*/
OXGN_DATA_NDAPI auto EncodeLooseCookedIndex(
  const IndexContent& content, serio::AnyWriter& writer) -> Result<void>;

} // namespace oxygen::data::loose_cooked
