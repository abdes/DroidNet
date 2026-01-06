//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::content::import::util {

//! Row pitch alignment required by D3D12 texture uploads.
inline constexpr uint64_t kRowPitchAlignment = 256;

//! Number of bytes per pixel in RGBA8 format.
inline constexpr uint64_t kBytesPerPixelRgba8 = 4;

//! Default buffer alignment for resource data files.
inline constexpr uint64_t kDefaultBufferAlignment = 16;

} // namespace oxygen::content::import::util
