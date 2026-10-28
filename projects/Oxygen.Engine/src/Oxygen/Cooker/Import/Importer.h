//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

struct ImportRequest;

//! The importer that cooks a request: one per kind of import job.
/*!
 The importers and their revisions are listed in `PakFormatVersions.inc`, the
 catalog of every version cooked content depends on.
*/
// The importers are an X-macro list in the version catalog, so that one list
// holds them and their revisions; these macros are the only way to expand it.
// Each enumerator the expansion emits carries its own trailing comma.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,readability-trailing-comma)
enum class Importer : uint8_t {
#define OXDAT_FORMAT_VERSION(name, spec_name, type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value) importer,
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
};

//! Every importer, in catalog order.
inline constexpr auto kImporters = std::to_array<Importer>({
#define OXDAT_FORMAT_VERSION(name, spec_name, type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value) Importer::importer,
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
});
// NOLINTEND(cppcoreguidelines-macro-usage,readability-trailing-comma)

//! The importer's name, as the ImportTool reports it.
OXGN_COOK_NDAPI auto to_string(Importer importer) -> std::string_view;

//! The revision of what the importer cooks.
OXGN_COOK_NDAPI auto RevisionOf(Importer importer) -> uint32_t;

//! The importer that cooks `request`, or nullopt when no importer handles it,
//! such as a model whose source format is unknown.
OXGN_COOK_NDAPI auto ImporterOf(const ImportRequest& request)
  -> std::optional<Importer>;

} // namespace oxygen::content::import
