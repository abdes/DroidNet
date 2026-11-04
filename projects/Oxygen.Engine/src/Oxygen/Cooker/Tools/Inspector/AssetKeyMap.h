//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::clap {
class Command;
}

namespace oxygen::content::inspection {

//! The version of the `oxygen.asset-key-map` report.
inline constexpr uint32_t kAssetKeyMapSchemaVersion = 2;
//! The version of the `oxygen.asset-key-request` file.
inline constexpr uint32_t kAssetKeyRequestSchemaVersion = 2;

struct AssetKeyMapOptions {
  std::string input;
  std::string output;
};

auto BuildAssetKeyMapCommand(AssetKeyMapOptions& options)
  -> std::shared_ptr<clap::Command>;
[[nodiscard]] auto RunAssetKeyMap(const AssetKeyMapOptions& options)
  -> toolkit::ToolResult;

} // namespace oxygen::content::inspection
