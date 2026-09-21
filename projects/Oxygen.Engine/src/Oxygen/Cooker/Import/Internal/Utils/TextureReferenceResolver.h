//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {
class IAsyncFileReader;
class ImportSession;
struct ImportRequest;

namespace internal {

  struct TextureReferenceRequest {
    std::string virtual_path;
    std::string object_path;
    std::string diagnostic_prefix;
  };

  struct ResolvedTextureReference {
    data::pak::core::ResourceIndexT index { data::pak::core::kNoResourceIndex };
    data::pak::core::TextureResourceDesc descriptor {};
    std::filesystem::path cooked_root;
  };

  //! Resolve an authored texture descriptor using the existing mounted-root
  //! policy.
  [[nodiscard]] auto ResolveTextureReference(
    observer_ptr<ImportSession> session,
    observer_ptr<const ImportRequest> request,
    observer_ptr<IAsyncFileReader> reader, TextureReferenceRequest reference)
    -> co::Co<std::optional<ResolvedTextureReference>>;

} // namespace internal
} // namespace oxygen::content::import
