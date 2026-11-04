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
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {
class CapturedInputSet;

//! Exact bytes consumed by synchronous request preparation, with a pending
//! proof.
struct ImportSourceDocument final {
  std::filesystem::path path;
  std::string text;
  base::Sha256Digest digest {};

  //! Reads the document; a refusal has a `request_codes` `kDocument*` code.
  OXGN_COOK_NDAPI static auto Load(const std::filesystem::path& path,
    std::string_view kind, std::vector<ImportDiagnostic>& diagnostics,
    const CapturedInputSet* captured_inputs = nullptr)
    -> std::optional<ImportSourceDocument>;

  OXGN_COOK_NDAPI auto Observation() const -> ImportSourceObservation;
};

} // namespace oxygen::content::import
