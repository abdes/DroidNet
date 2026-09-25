// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>

namespace oxygen::examples::render_scene {

//! Locations for the source-tree demo or the installed, relocatable showcase.
struct RuntimePaths {
  bool installed { false };
  std::filesystem::path root;
  std::filesystem::path showcase;
  std::filesystem::path content;
};

[[nodiscard]] auto ResolveRuntimePaths() -> RuntimePaths;

} // namespace oxygen::examples::render_scene
