//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content {

namespace {

  //! One failed requirement, such as `Material 0e1f... of 'Engine'
  //! (type_mismatch: Geometry)`.
  auto Describe(const FailedRequirement& failed) -> std::string
  {
    const auto& requirement = failed.requirement;
    auto text = requirement.kind == data::KeyReferenceKind::kTexture
      ? std::string("texture")
      : std::string(data::to_string(requirement.type));
    text += " " + data::to_string(requirement.key) + " of '" + failed.library
      + "' (" + std::string(to_string(failed.failure));
    if (failed.found_type.has_value()) {
      text += ": " + std::string(data::to_string(*failed.found_type));
    }
    if (!failed.found_library.empty()) {
      text += ": '" + failed.found_library + "'";
    }
    return text + ")";
  }

  auto Message(const std::filesystem::path& pak,
    const std::vector<FailedRequirement>& failures) -> std::string
  {
    auto text = "'" + pak.generic_string()
      + "' requires what the layers below it do not provide: ";
    for (size_t index = 0; index < failures.size(); ++index) {
      text += (index == 0 ? "" : "; ") + Describe(failures.at(index));
    }
    return text;
  }

} // namespace

auto to_string(const RequirementFailure value) noexcept -> std::string_view
{
  switch (value) {
  case RequirementFailure::kMissing:
    return "missing";
  case RequirementFailure::kTombstoned:
    return "tombstoned";
  case RequirementFailure::kTypeMismatch:
    return "type_mismatch";
  case RequirementFailure::kLibraryMismatch:
    return "library_mismatch";
  }
  return "unknown";
}

DependencyRequirementError::DependencyRequirementError(
  std::filesystem::path pak, std::vector<FailedRequirement> failures)
  : std::runtime_error(Message(pak, failures))
  , pak_(std::move(pak))
  , failures_(std::move(failures))
{
}

} // namespace oxygen::content
