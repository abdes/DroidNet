//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <string_view>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Importer.h>

namespace oxygen::content::import {

// The importers are an X-macro list in the version catalog, so that one list
// holds them and their revisions; these macros are the only way to expand it.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
auto to_string(const Importer importer) -> std::string_view
{
  switch (importer) {
#define OXDAT_FORMAT_VERSION(name, spec_name, type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value)                    \
  case Importer::importer:                                                     \
    return #spec_name;
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
  }
  return "__NotSupported__";
}

auto RevisionOf(const Importer importer) -> uint32_t
{
  switch (importer) {
#define OXDAT_FORMAT_VERSION(name, spec_name, type, value)
#define OXDAT_ASSET_VERSION(name, asset_type, spec_name, value)
#define OXDAT_IMPORTER_REVISION(importer, spec_name, value)                    \
  case Importer::importer:                                                     \
    return value;
#include <Oxygen/Data/PakFormatVersions.inc>
#undef OXDAT_IMPORTER_REVISION
#undef OXDAT_ASSET_VERSION
#undef OXDAT_FORMAT_VERSION
  }
  return 0;
}
// NOLINTEND(cppcoreguidelines-macro-usage)

auto ImporterOf(const ImportRequest& request) -> std::optional<Importer>
{
  // A request carries the payload of the one importer that cooks it; one
  // without a payload is a source file, cooked by its format's importer.
  if (request.input.has_value()) {
    return Importer::kInput;
  }
  if (request.buffer_container.has_value()) {
    return Importer::kBufferContainer;
  }
  if (request.material_descriptor.has_value()) {
    return Importer::kMaterialDescriptor;
  }
  if (request.physics_material_descriptor.has_value()) {
    return Importer::kPhysicsMaterialDescriptor;
  }
  if (request.collision_shape_descriptor.has_value()) {
    return Importer::kCollisionShapeDescriptor;
  }
  if (request.geometry_descriptor.has_value()) {
    return Importer::kGeometryDescriptor;
  }
  if (request.scene_descriptor.has_value()) {
    return Importer::kSceneDescriptor;
  }
  switch (request.options.scripting.import_kind) {
  case ScriptingImportKind::kScriptAsset:
    return Importer::kScriptAsset;
  case ScriptingImportKind::kScriptingSidecar:
    return Importer::kScriptingSidecar;
  case ScriptingImportKind::kNone:
    break;
  }
  if (request.physics.has_value()) {
    return Importer::kPhysicsSidecar;
  }
  switch (request.GetFormat()) {
  case ImportFormat::kGltf:
    return Importer::kGltf;
  case ImportFormat::kFbx:
    return Importer::kFbx;
  case ImportFormat::kTextureImage:
    return Importer::kTexture;
  case ImportFormat::kUnknown:
    break;
  }
  return std::nullopt;
}

} // namespace oxygen::content::import
