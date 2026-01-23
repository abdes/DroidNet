//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/TextureImportSettings.h>

namespace oxygen::content::import::internal {

auto ParseIntent(std::string_view value) -> std::optional<TextureIntent>;
auto ParseColorSpace(std::string_view value) -> std::optional<ColorSpace>;
auto ParseFormat(std::string_view value) -> std::optional<Format>;
auto ParseMipPolicy(std::string_view value) -> std::optional<MipPolicy>;
auto ParseMipFilter(std::string_view value) -> std::optional<MipFilter>;
auto ParseBc7Quality(std::string_view value) -> std::optional<Bc7Quality>;
auto ParseHdrHandling(std::string_view value) -> std::optional<HdrHandling>;
auto ParseCubeLayout(std::string_view value)
  -> std::optional<CubeMapImageLayout>;

auto MapSettingsToTuning(const TextureImportSettings& settings,
  ImportOptions::TextureTuning& tuning, std::ostream& error_stream) -> bool;

} // namespace oxygen::content::import::internal
