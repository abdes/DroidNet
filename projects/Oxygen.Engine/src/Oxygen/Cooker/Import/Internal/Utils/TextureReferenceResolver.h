//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {
class IAsyncFileReader;
class ImportSession;
struct ImportRequest;

namespace internal {

  struct TextureReferenceRequest {
    //! The reference as written in the job's authored file.
    std::string text;
    std::string json_pointer;
    //! The codes of a descriptor that cannot be used, and of one that is gone.
    std::string_view descriptor_invalid_code;
    std::string_view descriptor_missing_code;
    //! A texture whose descriptor no longer exists, for example after it was
    //! deleted, is reported as a warning and resolved as missing instead of
    //! failing the import.
    bool allow_missing { false };
  };

  struct ResolvedTextureReference {
    //! The job's own texture-table entry, or the key of a texture a cooked
    //! context root holds; empty when the texture is missing.
    data::TextureTarget target;
    data::pak::core::TextureResourceDesc descriptor {};
    //! True when the descriptor was not found and the request allowed it; the
    //! caller cooks without the texture.
    bool missing { false };
  };

  //! Resolve an authored texture reference against the job's identity
  //! (`ResolveJobReference`), then look it up in the indexes of the mounted
  //! cooked roots, newest first: in the job's own output root it binds a local
  //! texture-table entry, in a cooked context root it binds the texture by key
  //! without copying it. Returns `std::nullopt` after reporting an error
  //! diagnostic; a missing descriptor resolves as `missing` when the request
  //! allows it.
  [[nodiscard]] auto ResolveTextureReference(
    observer_ptr<ImportSession> session,
    observer_ptr<const ImportRequest> request,
    std::span<const MountedCookedSource> mounts,
    TextureReferenceRequest reference)
    -> co::Co<std::optional<ResolvedTextureReference>>;

} // namespace internal
} // namespace oxygen::content::import
