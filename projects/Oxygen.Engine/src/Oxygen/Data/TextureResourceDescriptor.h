//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Decoded current-format loose texture descriptor, relative to its source.
struct TextureResourceDescriptor {
  pak::core::ResourceIndexT index { pak::core::kNoResourceIndex };
  pak::core::TextureResourceDesc descriptor {};
};

inline constexpr std::size_t kTextureResourceDescriptorSize
  = 12U + sizeof(pak::core::TextureResourceDesc);

//! Decode an OTEX sidecar, rejecting obsolete layouts and reserved values.
OXGN_DATA_NDAPI auto DecodeTextureResourceDescriptor(
  std::span<const std::byte> bytes)
  -> Result<TextureResourceDescriptor, std::string>;

} // namespace oxygen::data
