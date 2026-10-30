//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace oxygen::data {

//! Why a cooked container, a pak or a loose cooked index, cannot be opened.
enum class CookedOpenFailure : uint8_t {
  //! Not a file of that format: its magic differs, or it is too small to
  //! hold one.
  kWrongFormat = 0,
  //! A format version this build cannot read.
  kUnsupportedVersion,
  //! Of that format and version, but malformed.
  kCorrupt,
};

//! A cooked container that cannot be opened, and why.
class CookedOpenError final : public std::runtime_error {
public:
  CookedOpenError(const CookedOpenFailure failure, const std::string& message)
    : std::runtime_error(message)
    , failure_(failure)
  {
  }

  [[nodiscard]] auto Failure() const noexcept -> CookedOpenFailure
  {
    return failure_;
  }

private:
  CookedOpenFailure failure_;
};

} // namespace oxygen::data
