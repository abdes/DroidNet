//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::data {

struct PatchCompatibilityEnvelope final {
  std::vector<PakCatalogBase> required_base_layers;
  uint16_t patch_content_version = 0;
};

struct PatchManifest final {
  std::vector<AssetKey> created;
  std::vector<AssetKey> replaced;
  std::vector<AssetKey> deleted;

  PatchCompatibilityEnvelope compatibility_envelope {};

  std::string diff_basis_identifier = "descriptor_plus_transitive_resources_v1";

  SourceKey patch_source_key {};
  std::optional<std::array<uint8_t, 32>> patch_pak_digest;
  std::optional<uint32_t> patch_pak_crc32;
};

} // namespace oxygen::data
