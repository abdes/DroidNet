//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Authoring-time locator for a texture within an explicitly mounted source.
struct TextureResourceLocator {
  std::filesystem::path cooked_root;
  std::filesystem::path descriptor_relative_path;
};

//! Find the exact descriptor or its unique current hashed filename.
//! Ambiguous matches throw; a missing descriptor returns nullopt.
OXGN_CNTT_NDAPI auto FindTextureResourceDescriptorPath(
  const std::filesystem::path& descriptor_path)
  -> std::optional<std::filesystem::path>;

} // namespace oxygen::content
