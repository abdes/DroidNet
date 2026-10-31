//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content {

//! Why a pak's requirement is not met by the layers mounted below it.
enum class RequirementFailure : uint8_t {
  //! No layer below provides it.
  kMissing = 0,
  //! A layer below deletes it.
  kTombstoned,
  //! The providing layer holds an asset of another type.
  kTypeMismatch,
  //! The providing layer's entry lies under another mount.
  kMountMismatch,
};

OXGN_CNTT_NDAPI auto to_string(RequirementFailure value) noexcept
  -> std::string_view;

//! A requirement the layers below a pak do not meet, and why.
struct FailedRequirement final {
  //! The mount the pak requires it of.
  std::string mount;
  data::PakRequirement requirement;
  RequirementFailure failure = RequirementFailure::kMissing;
  //! The type found, for `kTypeMismatch`.
  std::optional<data::AssetType> found_type;
  //! The mount found, for `kMountMismatch`.
  std::string found_mount;
};

//! A pak whose requirements the layers mounted below it do not meet.
/*!
 Thrown by a mount, which then leaves the mounted layers unchanged. The
 message names the pak and every failed requirement.
*/
class DependencyRequirementError final : public std::runtime_error {
public:
  OXGN_CNTT_API DependencyRequirementError(
    std::filesystem::path pak, std::vector<FailedRequirement> failures);

  //! The pak whose requirements failed.
  [[nodiscard]] auto Pak() const noexcept -> const std::filesystem::path&
  {
    return pak_;
  }

  //! Every failed requirement, in the pak's catalog order.
  [[nodiscard]] auto Failures() const noexcept
    -> const std::vector<FailedRequirement>&
  {
    return failures_;
  }

private:
  std::filesystem::path pak_;
  std::vector<FailedRequirement> failures_;
};

} // namespace oxygen::content
