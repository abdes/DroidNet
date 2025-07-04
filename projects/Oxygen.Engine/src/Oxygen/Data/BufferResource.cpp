//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/BufferResource.h>

using oxygen::data::BufferResource;

auto BufferResource::GetElementFormat() const noexcept -> Format
{
  static_assert(
    static_cast<std::underlying_type_t<Format>>(Format::kUnknown) == 0,
    "Format::kUnknown must be 0 for correct raw buffer detection");

  if (desc_.element_format
      >= static_cast<std::underlying_type_t<Format>>(Format::kUnknown)
    && desc_.element_format
      <= static_cast<std::underlying_type_t<Format>>(Format::kMaxFormat)) {
    return static_cast<Format>(desc_.element_format);
  }
  LOG_F(WARNING, "Invalid element format: {}", desc_.element_format);
  return Format::kUnknown;
}
