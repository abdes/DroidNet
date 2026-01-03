//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::content::import {

//! Supported authoring source formats.
enum class ImportFormat : uint8_t {
  kUnknown = 0,
  kGltf,
  kGlb,
  kFbx,
};

} // namespace oxygen::content::import
