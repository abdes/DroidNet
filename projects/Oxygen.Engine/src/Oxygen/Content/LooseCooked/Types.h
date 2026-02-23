//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

namespace oxygen::content::lc {

using FileKind = data::loose_cooked::FileKind;

struct AssetEntry {
  data::AssetKey key {};
  std::string virtual_path;
  std::string descriptor_relpath;
  uint64_t descriptor_size = 0;
  uint8_t asset_type = 0;
  std::optional<base::Sha256Digest> descriptor_sha256;
};

struct FileEntry {
  FileKind kind = FileKind::kUnknown;
  std::string relpath;
  uint64_t size = 0;
};

} // namespace oxygen::content::lc
