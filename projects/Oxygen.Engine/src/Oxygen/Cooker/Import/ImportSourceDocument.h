//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
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

  OXGN_COOK_NDAPI static auto Load(const std::filesystem::path& path,
    std::string_view kind, std::ostream& errors,
    const CapturedInputSet* captured_inputs = nullptr)
    -> std::optional<ImportSourceDocument>;

  OXGN_COOK_NDAPI auto Observation() const -> ImportSourceObservation;
};

} // namespace oxygen::content::import
