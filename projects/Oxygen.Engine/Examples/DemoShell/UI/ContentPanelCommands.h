//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;

//! The content panel's commands; every control of the panel runs one.
namespace content_commands {
  // clang-format off
  // Mounts.
  inline constexpr std::string_view kAddPak = "demo.content.add_pak";
  inline constexpr std::string_view kAddCookedRoot = "demo.content.add_cooked_root";
  inline constexpr std::string_view kRemoveSource = "demo.content.remove_source";
  inline constexpr std::string_view kMoveSource = "demo.content.move_source";
  inline constexpr std::string_view kAddSourceFolder = "demo.content.add_source_folder";
  inline constexpr std::string_view kRemoveSourceFolder = "demo.content.remove_source_folder";
  inline constexpr std::string_view kRemount = "demo.content.remount";
  //! The engine's own command; the Mounts tab's button is a shortcut to it.
  inline constexpr std::string_view kTrimCache = "cntt.trim_cache";

  // Scenes.
  inline constexpr std::string_view kLoadScene = "demo.content.load_scene";
  inline constexpr std::string_view kReloadScene = "demo.content.reload_scene";
  inline constexpr std::string_view kUnloadScene = "demo.content.unload_scene";
  inline constexpr std::string_view kCancelSceneLoad = "demo.content.cancel_scene_load";

  // Import.
  inline constexpr std::string_view kImport = "demo.content.import";
  inline constexpr std::string_view kImportAsIs = "demo.content.import_as_is";
  inline constexpr std::string_view kImportFile = "demo.content.import_file";
  inline constexpr std::string_view kCancelImport = "demo.content.cancel_import";
  inline constexpr std::string_view kRefreshModels = "demo.content.refresh_models";
  inline constexpr std::string_view kSourceMount = "demo.content.import_tab.source_mount";
  inline constexpr std::string_view kModelFilter = "demo.content.import_tab.filter";
  inline constexpr std::string_view kListFbx = "demo.content.import_tab.fbx";
  inline constexpr std::string_view kListGlb = "demo.content.import_tab.glb";
  inline constexpr std::string_view kListGltf = "demo.content.import_tab.gltf";
  inline constexpr std::string_view kLoadAfterImport = "demo.content.import_tab.load_scene_after_import";
  inline constexpr std::string_view kResetGroup = "demo.content.import.reset_group";
  inline constexpr std::string_view kRestartImporter = "demo.content.restart_importer";

  // Import settings: Content.
  inline constexpr std::string_view kImportTextures = "demo.content.import.textures";
  inline constexpr std::string_view kImportMaterials = "demo.content.import.materials";
  inline constexpr std::string_view kImportGeometry = "demo.content.import.geometry";
  inline constexpr std::string_view kImportScene = "demo.content.import.scene";
  inline constexpr std::string_view kIgnoreNonMesh = "demo.content.import.ignore_non_mesh";
  inline constexpr std::string_view kNodePruning = "demo.content.import.node_pruning";

  // Import settings: Geometry.
  inline constexpr std::string_view kUnits = "demo.content.import.units";
  inline constexpr std::string_view kUnitScale = "demo.content.import.unit_scale";
  inline constexpr std::string_view kUpAxis = "demo.content.import.up_axis";
  inline constexpr std::string_view kNormalPolicy = "demo.content.import.normal_policy";
  inline constexpr std::string_view kTangentPolicy = "demo.content.import.tangent_policy";
  inline constexpr std::string_view kSmoothingAngle = "demo.content.import.smoothing_angle";
  inline constexpr std::string_view kGenerateLods = "demo.content.import.generate_lods";
  inline constexpr std::string_view kLodLevels = "demo.content.import.lod_levels";
  inline constexpr std::string_view kLodReduction = "demo.content.import.lod_reduction";

  // Import settings: Lights and cameras.
  inline constexpr std::string_view kImportLights = "demo.content.import.lights";
  inline constexpr std::string_view kImportCameras = "demo.content.import.cameras";
  inline constexpr std::string_view kLightScale = "demo.content.import.light_intensity_scale";

  // Import settings: Textures.
  inline constexpr std::string_view kTuning = "demo.content.texture.enabled";
  inline constexpr std::string_view kIntent = "demo.content.texture.intent";
  inline constexpr std::string_view kColorSpace = "demo.content.texture.color_space";
  inline constexpr std::string_view kMipPolicy = "demo.content.texture.mip_policy";
  inline constexpr std::string_view kMaxMips = "demo.content.texture.max_mips";
  inline constexpr std::string_view kMaxSize = "demo.content.texture.max_size";
  inline constexpr std::string_view kMipFilter = "demo.content.texture.mip_filter";
  inline constexpr std::string_view kColorFormat = "demo.content.texture.color_format";
  inline constexpr std::string_view kDataFormat = "demo.content.texture.data_format";
  inline constexpr std::string_view kBc7Quality = "demo.content.texture.bc7_quality";
  inline constexpr std::string_view kHdrHandling = "demo.content.texture.hdr_handling";
  inline constexpr std::string_view kBakeHdr = "demo.content.texture.bake_hdr";
  inline constexpr std::string_view kExposureEv = "demo.content.texture.exposure_ev";
  inline constexpr std::string_view kFlipGreen = "demo.content.texture.flip_normal_green";
  inline constexpr std::string_view kRenormalize = "demo.content.texture.renormalize_mips";
  inline constexpr std::string_view kCubemap = "demo.content.texture.cubemap";
  inline constexpr std::string_view kEquirect = "demo.content.texture.equirect";
  inline constexpr std::string_view kFaceSize = "demo.content.texture.face_size";
  inline constexpr std::string_view kCubeLayout = "demo.content.texture.cube_layout";

  // Importer: thread pool; pipelines are `demo.content.importer.<name>.workers`
  // and `.queue`.
  inline constexpr std::string_view kThreadPool = "demo.content.importer.threads";
  inline constexpr std::string_view kPipelinePrefix = "demo.content.importer.";

  // Log.
  inline constexpr std::string_view kClearLog = "demo.content.clear_log";
  // clang-format off
} // namespace content_commands

//! Registers every content panel command with `commands`, acting on `vm`.
auto RegisterContentCommands(PanelCommands& commands, observer_ptr<ContentVm> vm)
  -> void;

} // namespace oxygen::examples::ui
