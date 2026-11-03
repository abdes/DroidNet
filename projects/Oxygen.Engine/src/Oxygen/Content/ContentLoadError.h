//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Why a load failed although its descriptors follow their formats.
enum class ContentLoadFailure : uint8_t {
  //! A descriptor follows its format but breaks a rule of the engine, such as
  //! an input mapping on a slot the platform does not define.
  kContentInvalid = 0,
  //! A dependency the asset requires is not in the load's mounts.
  kDependencyMissing,
  //! A dependency the asset requires failed its own load; its error is
  //! nested (catch `std::nested_exception`) and its message ends this one.
  kDependencyFailed,
  //! A reference resolves, in the load's mounts, to content of another kind.
  kReferenceInvalid,
  //! The mounts hold content that cannot be bound to the asset, such as two
  //! physics sidecars that target one scene.
  kBindingFailed,
};

OXGN_CNTT_NDAPI auto to_string(ContentLoadFailure value) noexcept
  -> std::string_view;

//! A load that failed although its descriptors follow their formats: content
//! the engine refuses, or what the mounts lack or hold wrongly; and a message
//! for logs.
/*!
 A descriptor that breaks a rule of its format is a `data::CookedFormatError`
 instead. Not `final`: `std::throw_with_nested` nests a cause only in a
 derivable type.
*/
class ContentLoadError : public std::runtime_error {
public:
  ContentLoadError(const ContentLoadFailure failure, const std::string& message)
    : std::runtime_error(message)
    , failure_(failure)
  {
  }

  //! Why the load failed.
  [[nodiscard]] auto Failure() const noexcept -> ContentLoadFailure
  {
    return failure_;
  }

private:
  ContentLoadFailure failure_;
};

} // namespace oxygen::content
