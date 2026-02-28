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
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::data {

struct PatchCompatibilityPolicySnapshot final {
  bool require_exact_base_set = true;
  bool require_content_version_match = true;
  bool require_base_source_key_match = true;
  bool require_catalog_digest_match = true;
};

struct PatchCompatibilityEnvelope final {
  std::vector<SourceKey> required_base_source_keys;
  std::vector<uint16_t> required_base_content_versions;
  std::vector<std::array<uint8_t, 32>> required_base_catalog_digests;
  uint16_t patch_content_version = 0;
};

struct PatchManifest final {
  std::vector<AssetKey> created;
  std::vector<AssetKey> replaced;
  std::vector<AssetKey> deleted;

  PatchCompatibilityEnvelope compatibility_envelope {};
  PatchCompatibilityPolicySnapshot compatibility_policy_snapshot {};

  std::string diff_basis_identifier = "descriptor_plus_transitive_resources_v1";

  SourceKey patch_source_key {};
  std::optional<std::array<uint8_t, 32>> patch_pak_digest;
  std::optional<uint32_t> patch_pak_crc32;
};

} // namespace oxygen::data
