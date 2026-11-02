//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <string>

#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content {

//! A cooked source whose SourceKey another mounted source already has.
/*!
 A SourceKey names one mounted source, so a second source with it would make
 every lookup by SourceKey ambiguous. Thrown by a mount, which then leaves the
 mounted layers unchanged. A nil SourceKey never conflicts.
*/
class SourceKeyConflictError final : public std::runtime_error {
public:
  OXGN_CNTT_API SourceKeyConflictError(
    data::SourceKey source_key, std::string mount, std::string existing_mount);

  //! The SourceKey both sources have.
  [[nodiscard]] auto GetSourceKey() const noexcept -> const data::SourceKey&
  {
    return source_key_;
  }

  //! The source that was refused.
  [[nodiscard]] auto Mount() const noexcept -> const std::string&
  {
    return mount_;
  }

  //! The mounted source that already has the SourceKey.
  [[nodiscard]] auto ExistingMount() const noexcept -> const std::string&
  {
    return existing_mount_;
  }

private:
  data::SourceKey source_key_;
  std::string mount_;
  std::string existing_mount_;
};

} // namespace oxygen::content
