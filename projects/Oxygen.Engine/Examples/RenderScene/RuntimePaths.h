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
  //! The examples project root.
  std::filesystem::path content;
  //! The project's `Content` mount folder.
  std::filesystem::path content_mount;
  //! Where RenderScene's `Tools/cook` cooks the examples:
  //! `<showcase>/.cooked/Content`.
  std::filesystem::path cooked;
  //! Where its `Tools/pak` packages them: `<showcase>/.build/pak`.
  std::filesystem::path paks;
  //! RenderScene's scratch project of ad-hoc model imports:
  //! `<showcase>/.scratch`.
  std::filesystem::path scratch;
};

[[nodiscard]] auto ResolveRuntimePaths() -> RuntimePaths;

} // namespace oxygen::examples::render_scene
