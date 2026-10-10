//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::cooker::test {

//! Asset key whose first byte is `seed` and all other bytes are zero.
inline auto MakeFirstByteAssetKey(const std::uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<std::uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
  return data::AssetKey::FromBytes(bytes);
}

//! Builds a scene string table: a leading NUL followed by NUL-terminated text.
struct SceneStringTableBuilder final {
  std::vector<std::byte> bytes { std::byte { 0 } };

  auto Add(std::string_view text) -> data::pak::core::StringTableOffsetT
  {
    const auto offset
      = static_cast<data::pak::core::StringTableOffsetT>(bytes.size());
    for (const char c : text) {
      bytes.push_back(std::byte { static_cast<unsigned char>(c) });
    }
    bytes.push_back(std::byte { 0 });
    return offset;
  }
};

//! Scene adapter that returns a prepared `SceneBuild`.
struct FakeSceneAdapter final {
  content::import::SceneBuild build;
  bool succeed = true;

  auto BuildSceneStage(const content::import::SceneStageInput& input,
    std::vector<content::import::ImportDiagnostic>& diagnostics) const
    -> content::import::SceneStageResult
  {
    static_cast<void>(input);
    static_cast<void>(diagnostics);
    return content::import::SceneStageResult {
      .build = build,
      .success = succeed,
    };
  }
};

//! Single-node scene build whose node is named `name`.
inline auto MakeMinimalSceneBuild(const std::string_view name)
  -> content::import::SceneBuild
{
  SceneStringTableBuilder strings;
  const auto name_offset = strings.Add(name);

  content::import::SceneBuild build;
  build.nodes.push_back(data::pak::world::NodeRecord {
    .node_id = MakeFirstByteAssetKey(1U),
    .scene_name_offset = name_offset,
    .parent_index = 0,
    .node_flags = 0,
    .translation = { 0.0F, 0.0F, 0.0F },
    .rotation = { 0.0F, 0.0F, 0.0F, 1.0F },
    .scale = { 1.0F, 1.0F, 1.0F },
  });
  build.strings = std::move(strings.bytes);
  return build;
}

} // namespace oxygen::cooker::test
