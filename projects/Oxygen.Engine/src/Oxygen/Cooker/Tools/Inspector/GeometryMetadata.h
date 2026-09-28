//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

namespace oxygen::clap {
class Command;
}

namespace oxygen::content::inspection {

struct GeometryMetadataOptions {
  std::string cooked_root;
  std::string output;
  std::string virtual_path;
};

auto BuildGeometryMetadataCommand(GeometryMetadataOptions& options)
  -> std::shared_ptr<clap::Command>;
auto RunGeometryMetadataReport(const GeometryMetadataOptions& options) -> int;

} // namespace oxygen::content::inspection
