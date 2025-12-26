//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include "DumpContext.h"

namespace oxygen::content {
class PakFile;
class AssetLoader;
} // namespace oxygen::content

class PakFileDumper {
public:
  explicit PakFileDumper(DumpContext ctx)
    : ctx_(std::move(ctx))
  {
  }

  void Dump(const oxygen::content::PakFile& pak,
    oxygen::content::AssetLoader& asset_loader);

private:
  DumpContext ctx_;

  void PrintPakHeader(const oxygen::content::PakFile& pak);

  void PrintPakFooter(const oxygen::content::PakFile& pak);
};
