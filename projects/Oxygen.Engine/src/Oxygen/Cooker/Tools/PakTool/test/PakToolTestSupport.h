//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetType.h>

//! The cooked content the PakTool functional tests feed the CLI.
/*!
 PakTool reads real files, so these tests stay on disk, but the content they
 pack is described with the in-memory pak kit and written out. Every root holds
 the mount `Game` with one material, because a pak names exactly one mount.
*/
namespace oxygen::content::pak::tool::test {

//! A loose cooked root of the mount `Game` that holds the material `name`.
[[nodiscard]] inline auto MakeGameRoot(const std::string_view name,
  const uint8_t seed = 1U) -> oxygen::cooker::testing::LooseCookedRoot
{
  auto root = oxygen::cooker::testing::LooseCookedRoot { "Game", seed };
  root.Add({
    .type = data::AssetType::kMaterial,
    .leaf = std::string(name) + ".omat",
    .descriptor = oxygen::cooker::testing::MaterialBuilder { name }.Bytes(),
  });
  return root;
}

//! Writes a loose cooked root of the mount `Game` holding the material `name`
//! into `folder`; returns `folder`.
inline auto WriteLooseGameRoot(const std::filesystem::path& folder,
  const std::string_view name) -> std::filesystem::path
{
  oxygen::cooker::testing::Materialize(MakeGameRoot(name).Files(), folder);
  return folder;
}

//! Packs a root of the mount `Game` holding the material `name` into the file
//! `pak_path`, under the pak identity `identity`; returns `pak_path`.
inline auto WriteGamePak(const std::filesystem::path& pak_path,
  const std::string_view name, const uint8_t identity = 0x70U)
  -> std::filesystem::path
{
  auto workspace = oxygen::cooker::testing::PakWorkspace {};
  auto request = workspace.FullRequest({ workspace.Add(MakeGameRoot(name)) });
  request.source_key = oxygen::cooker::testing::SourceKeyOf(identity);
  workspace.PackOrThrow(request);
  oxygen::cooker::testing::Materialize(
    workspace.Storage().Bytes(request.output_pak_path), pak_path);
  return pak_path;
}

} // namespace oxygen::content::pak::tool::test
