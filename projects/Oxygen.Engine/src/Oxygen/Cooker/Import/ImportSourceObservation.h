//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/FileInfo.h>

namespace oxygen::content::import {

//! Digest of a consumed byte range; max_bytes == 0 extends to end of file.
struct ImportSourceReadProof final {
  uint64_t offset = 0;
  uint64_t max_bytes = 0;
  base::Sha256Digest digest {};
};

//! Source facts owned by one analysis/import operation. Preparation supplies
//! pending facts; ImportSourceSnapshot::Observations exports verified facts.
struct ImportSourceObservation final {
  std::filesystem::path path;
  bool exists = false;
  std::optional<FileInfo> metadata;
  std::vector<ImportSourceReadProof> reads;
};

} // namespace oxygen::content::import
