//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

struct CapturedInputFile final {
  std::filesystem::path path;
  uint64_t size = 0;
  base::Sha256Digest digest {};
};

//! A logical source fact and, when read access is declared, its private bytes.
struct CapturedInput final {
  std::filesystem::path logical_path;
  bool exists = false;
  std::optional<FileInfo> metadata;
  std::optional<CapturedInputFile> file;
};

//! Immutable admission map. Logical identities never use capture-directory
//! paths.
class CapturedInputSet final {
public:
  OXGN_COOK_API explicit CapturedInputSet(
    std::span<const CapturedInput> inputs);
  ~CapturedInputSet() = default;
  OXYGEN_MAKE_NON_COPYABLE(CapturedInputSet)
  OXYGEN_MAKE_NON_MOVABLE(CapturedInputSet)

  OXGN_COOK_NDAPI auto Find(const std::filesystem::path& logical_path) const
    -> const CapturedInput*;

private:
  std::unordered_map<std::string, CapturedInput> inputs_;
};

} // namespace oxygen::content::import
