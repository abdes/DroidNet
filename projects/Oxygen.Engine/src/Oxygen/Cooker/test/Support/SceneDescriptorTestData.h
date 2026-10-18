//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/Internal/SceneSource.h>

namespace oxygen::content::import::test {

//! Author a current scene fixture; version-rejection tests change it
//! explicitly.
inline auto MakeCurrentSceneDescriptor(const std::string_view text)
  -> nlohmann::json
{
  auto descriptor = nlohmann::json::parse(text);
  descriptor["version"] = internal::kSceneDescriptorVersion;
  return descriptor;
}

} // namespace oxygen::content::import::test
