//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::examples::testing {

//! A loose cooked root of the `Showcase` mount with two scenes.
struct ShowcaseRoot final {
  std::filesystem::path root;
  data::AssetKey bottle;
  std::string bottle_path;
  data::AssetKey lantern;
  std::string lantern_path;
};

//! Writes the `Showcase` root into `folder` with the cooked content
//! builders: `Scenes/BottleOnBox/BottleOnBox.oscene` and
//! `Scenes/Lantern/Lantern.oscene`. Roots mounted together take distinct
//! `seed`s, the identities of their SourceKeys.
auto WriteShowcaseRoot(
  const std::filesystem::path& folder, std::uint8_t seed = 1U) -> ShowcaseRoot;

//! A pak of the `Props` mount whose material references the default
//! material of the `Base` mount, which it requires.
struct DependentPak final {
  std::filesystem::path pak;
  //! The pak `pak` requires, holding the `Base` material.
  std::filesystem::path base_pak;
  //! The `Base` material the pak requires.
  data::AssetKey required;
};

//! Writes `props.pak` and `base.pak` into `folder`. Mounted without
//! `base.pak` below it, `props.pak` is refused: its one requirement is
//! missing.
auto WriteDependentPak(const std::filesystem::path& folder) -> DependentPak;

} // namespace oxygen::examples::testing
