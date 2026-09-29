//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/Internal/ImportSessionToken.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>

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
  struct Publication {
    data::SourceKey source_key {};
    std::optional<LooseCookedWriteResult> write_result {};
  };

  OXGN_COOK_API LooseCookedIndexRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedIndexRegistry)
  OXYGEN_MAKE_NON_MOVABLE(LooseCookedIndexRegistry)

  //! Register a new session for the cooked root.
  OXGN_COOK_NDAPI auto BeginSession(const std::filesystem::path& cooked_root,
    const std::optional<data::SourceKey>& source_key) -> ImportSessionToken;

  //! Register a file record in the shared index writer.
  OXGN_COOK_API auto RegisterExternalFile(
    const std::filesystem::path& cooked_root, data::loose_cooked::FileKind kind,
    std::string_view relpath) -> void;

  //! Register an asset record in the shared index writer.
  OXGN_COOK_API auto RegisterExternalAssetDescriptor(
    const std::filesystem::path& cooked_root, const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    const std::optional<base::Sha256Digest>& descriptor_sha256 = std::nullopt)
    -> void;

  //! Retire participation and await the cohort's index publication.
  /*! All participants receive the published identity. Only the writing session
      receives the full write result. Publication failure reaches every waiter.
   */
  OXGN_COOK_API auto EndSession(ImportSessionToken& participation)
    -> co::Co<Publication>;

  OXGN_COOK_API auto AbortSession(ImportSessionToken& participation) -> void;

private:
  struct Completion {
    co::Event ready {};
    data::SourceKey source_key {};
    std::exception_ptr failure {};
    bool aborted = false;
  };

  struct Entry {
    std::mutex mutex;
    std::unique_ptr<LooseCookedWriter> writer {};
    uint32_t active_sessions = 0;
    bool aborted = false;
    std::optional<data::SourceKey> source_key {};
    std::shared_ptr<Completion> completion {};
  };

  auto NormalizeKey(const std::filesystem::path& cooked_root) const
    -> std::string;

  auto GetEntry(const std::filesystem::path& cooked_root)
    -> std::shared_ptr<Entry>;

  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
};

} // namespace oxygen::content::import
