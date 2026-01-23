//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! Registry that aggregates loose cooked index updates per cooked root.
/*!
 Keeps a shared `LooseCookedWriter` per cooked root so that multiple
 concurrent import sessions can register assets and files without clobbering
 `container.index.bin`. The index is written only when the last session
 finishes.

 @see LooseCookedWriter
*/
class LooseCookedIndexRegistry final {
public:
  OXGN_CNTT_API LooseCookedIndexRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedIndexRegistry)
  OXYGEN_MAKE_NON_MOVABLE(LooseCookedIndexRegistry)

  //! Register a new session for the cooked root.
  OXGN_CNTT_API auto BeginSession(const std::filesystem::path& cooked_root,
    const std::optional<data::SourceKey>& source_key) -> void;

  //! Register a file record in the shared index writer.
  OXGN_CNTT_API auto RegisterExternalFile(
    const std::filesystem::path& cooked_root,
    data::loose_cooked::v1::FileKind kind, std::string_view relpath) -> void;

  //! Register an asset record in the shared index writer.
  OXGN_CNTT_API auto RegisterExternalAssetDescriptor(
    const std::filesystem::path& cooked_root, const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    const std::optional<base::Sha256Digest>& descriptor_sha256 = std::nullopt)
    -> void;

  //! Complete a session and write the index if this was the last session.
  OXGN_CNTT_API auto EndSession(const std::filesystem::path& cooked_root)
    -> std::optional<LooseCookedWriteResult>;

private:
  struct Entry {
    std::unique_ptr<LooseCookedWriter> writer;
    uint32_t active_sessions = 0;
    std::optional<data::SourceKey> source_key;
  };

  auto NormalizeKey(const std::filesystem::path& cooked_root) const
    -> std::string;

  std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

} // namespace oxygen::content::import
