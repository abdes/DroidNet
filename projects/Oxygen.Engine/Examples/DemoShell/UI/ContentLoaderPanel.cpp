//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/format.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui

namespace oxygen::examples::ui {

namespace {

  using content::import::AsyncImportService;
  using content::import::ImportContentFlags;
  using content::import::ImportOptions;
  using content::import::ImportPipelineConcurrency;
  using content::import::LooseCookedLayout;
  using TextureTuning = content::import::ImportOptions::TextureTuning;

  // Commands that act on the content library and the importer.
  constexpr std::string_view kModelRoot = "demo.content.model_root";
  constexpr std::string_view kRefreshSources = "demo.content.refresh_sources";
  constexpr std::string_view kImport = "demo.content.import";
  constexpr std::string_view kCancelImport = "demo.content.cancel_import";
  constexpr std::string_view kMountPak = "demo.content.mount_pak";
  constexpr std::string_view kLoadLibrary = "demo.content.load_library";
  constexpr std::string_view kUnloadAll = "demo.content.unload_all";
  constexpr std::string_view kRefreshLibrary = "demo.content.refresh_library";
  constexpr std::string_view kLoadScene = "demo.content.load_scene";
  constexpr std::string_view kCancelSceneLoad
    = "demo.content.cancel_scene_load";
  constexpr std::string_view kClearDiagnostics
    = "demo.content.clear_diagnostics";
  constexpr std::string_view kRestartImporter = "demo.content.restart_importer";
  //! The engine's own command; the panel's button is a shortcut to it.
  constexpr std::string_view kTrimCache = "cntt.trim_cache";

  // Explorer settings.
  constexpr std::string_view kIncludeFbx = "demo.content.include_fbx";
  constexpr std::string_view kIncludeGlb = "demo.content.include_glb";
  constexpr std::string_view kIncludeGltf = "demo.content.include_gltf";
  constexpr std::string_view kAutoLoad = "demo.content.auto_load_on_import";

  // Import options.
  constexpr std::string_view kImportTextures = "demo.content.import.textures";
  constexpr std::string_view kImportMaterials = "demo.content.import.materials";
  constexpr std::string_view kImportGeometry = "demo.content.import.geometry";
  constexpr std::string_view kImportScene = "demo.content.import.scene";
  constexpr std::string_view kHashing = "demo.content.import.hashing";
  constexpr std::string_view kIgnoreNonMesh
    = "demo.content.import.ignore_non_mesh";
  constexpr std::string_view kNodePruning = "demo.content.import.node_pruning";
  constexpr std::string_view kUnits = "demo.content.import.units";
  constexpr std::string_view kUnitScale = "demo.content.import.unit_scale";
  constexpr std::string_view kNormalPolicy
    = "demo.content.import.normal_policy";
  constexpr std::string_view kTangentPolicy
    = "demo.content.import.tangent_policy";
  constexpr std::string_view kUpAxis = "demo.content.import.up_axis";
  constexpr std::string_view kSmoothingAngle
    = "demo.content.import.smoothing_angle";
  constexpr std::string_view kGenerateLods
    = "demo.content.import.generate_lods";
  constexpr std::string_view kLodLevels = "demo.content.import.lod_levels";
  constexpr std::string_view kLodReduction
    = "demo.content.import.lod_reduction";
  constexpr std::string_view kImportLights = "demo.content.import.lights";
  constexpr std::string_view kImportCameras = "demo.content.import.cameras";
  constexpr std::string_view kLightScale
    = "demo.content.import.light_intensity_scale";

  // Texture tuning.
  constexpr std::string_view kTuning = "demo.content.texture.enabled";
  constexpr std::string_view kIntent = "demo.content.texture.intent";
  constexpr std::string_view kColorSpace = "demo.content.texture.color_space";
  constexpr std::string_view kMipPolicy = "demo.content.texture.mip_policy";
  constexpr std::string_view kMaxMips = "demo.content.texture.max_mips";
  constexpr std::string_view kMaxSize = "demo.content.texture.max_size";
  constexpr std::string_view kMipFilter = "demo.content.texture.mip_filter";
  constexpr std::string_view kColorFormat = "demo.content.texture.color_format";
  constexpr std::string_view kDataFormat = "demo.content.texture.data_format";
  constexpr std::string_view kBc7Quality = "demo.content.texture.bc7_quality";
  constexpr std::string_view kHdrHandling = "demo.content.texture.hdr_handling";
  constexpr std::string_view kBakeHdr = "demo.content.texture.bake_hdr";
  constexpr std::string_view kExposureEv = "demo.content.texture.exposure_ev";
  constexpr std::string_view kFlipGreen
    = "demo.content.texture.flip_normal_green";
  constexpr std::string_view kRenormalize
    = "demo.content.texture.renormalize_mips";
  constexpr std::string_view kCubemap = "demo.content.texture.cubemap";
  constexpr std::string_view kEquirect = "demo.content.texture.equirect";
  constexpr std::string_view kFaceSize = "demo.content.texture.face_size";
  constexpr std::string_view kCubeLayout = "demo.content.texture.cube_layout";

  // Importer service and output layout.
  constexpr std::string_view kThreadPool = "demo.content.importer.threads";

  constexpr auto kPruningModes = std::array {
    content::import::NodePruningPolicy::kKeepAll,
    content::import::NodePruningPolicy::kDropEmptyNodes,
  };
  constexpr auto kUnitPolicies = std::array {
    content::import::UnitNormalizationPolicy::kNormalizeToMeters,
    content::import::UnitNormalizationPolicy::kPreserveSource,
    content::import::UnitNormalizationPolicy::kApplyCustomFactor,
  };
  constexpr auto kUpAxes = std::array {
    content::import::SourceUpAxis::kFromFile,
    content::import::SourceUpAxis::kY,
    content::import::SourceUpAxis::kZ,
  };
  //! Texture size limits offered by the combo; 0 keeps the source size.
  constexpr auto kMaxSizes
    = std::array<uint32_t, 5> { 0U, 4096U, 2048U, 1024U, 512U };
  constexpr auto kGeometryPolicies = std::array {
    content::import::GeometryAttributePolicy::kNone,
    content::import::GeometryAttributePolicy::kPreserveIfPresent,
    content::import::GeometryAttributePolicy::kGenerateMissing,
    content::import::GeometryAttributePolicy::kAlwaysRecalculate,
  };
  constexpr auto kIntents = std::array {
    content::import::TextureIntent::kAlbedo,
    content::import::TextureIntent::kNormalTS,
    content::import::TextureIntent::kRoughness,
    content::import::TextureIntent::kMetallic,
    content::import::TextureIntent::kAO,
    content::import::TextureIntent::kEmissive,
    content::import::TextureIntent::kOpacity,
    content::import::TextureIntent::kORMPacked,
    content::import::TextureIntent::kHdrEnvironment,
    content::import::TextureIntent::kHdrLightProbe,
    content::import::TextureIntent::kData,
    content::import::TextureIntent::kHeightMap,
  };
  constexpr auto kColorSpaces = std::array {
    ColorSpace::kLinear,
    ColorSpace::kSRGB,
  };
  constexpr auto kMipPolicies = std::array {
    content::import::MipPolicy::kNone,
    content::import::MipPolicy::kFullChain,
    content::import::MipPolicy::kMaxCount,
  };
  constexpr auto kMipFilters = std::array {
    content::import::MipFilter::kBox,
    content::import::MipFilter::kKaiser,
    content::import::MipFilter::kLanczos,
  };
  constexpr auto kFormats = std::array {
    Format::kR8UNorm,
    Format::kR8SNorm,
    Format::kR16Float,
    Format::kR32Float,
    Format::kRG8UNorm,
    Format::kRG8SNorm,
    Format::kRG16Float,
    Format::kRG32Float,
    Format::kRGB32Float,
    Format::kRGBA8UNorm,
    Format::kRGBA8UNormSRGB,
    Format::kRGBA16Float,
    Format::kRGBA32Float,
    Format::kBC1UNorm,
    Format::kBC1UNormSRGB,
    Format::kBC2UNorm,
    Format::kBC2UNormSRGB,
    Format::kBC3UNorm,
    Format::kBC3UNormSRGB,
    Format::kBC4UNorm,
    Format::kBC5UNorm,
    Format::kBC6HFloatU,
    Format::kBC7UNorm,
    Format::kBC7UNormSRGB,
  };
  constexpr auto kBc7Tiers = std::array {
    content::import::Bc7Quality::kNone,
    content::import::Bc7Quality::kFast,
    content::import::Bc7Quality::kDefault,
    content::import::Bc7Quality::kHigh,
  };
  constexpr auto kHdrModes = std::array {
    content::import::HdrHandling::kError,
    content::import::HdrHandling::kTonemapAuto,
    content::import::HdrHandling::kKeepFloat,
  };
  constexpr auto kCubeLayouts = std::array {
    content::import::CubeMapImageLayout::kUnknown,
    content::import::CubeMapImageLayout::kAuto,
    content::import::CubeMapImageLayout::kHorizontalStrip,
    content::import::CubeMapImageLayout::kVerticalStrip,
    content::import::CubeMapImageLayout::kHorizontalCross,
    content::import::CubeMapImageLayout::kVerticalCross,
  };

  //! An importer pipeline, its command prefix and its config member.
  struct Pipeline {
    const char* label;
    std::string_view command;
    ImportPipelineConcurrency content::import::ImportConcurrency::* member;
  };
  constexpr auto kPipelines = std::array {
    Pipeline {
      .label = "Texture",
      .command = "demo.content.importer.texture",
      .member = &content::import::ImportConcurrency::texture,
    },
    Pipeline {
      .label = "Buffer",
      .command = "demo.content.importer.buffer",
      .member = &content::import::ImportConcurrency::buffer,
    },
    Pipeline {
      .label = "Material",
      .command = "demo.content.importer.material",
      .member = &content::import::ImportConcurrency::material,
    },
    Pipeline {
      .label = "Mesh",
      .command = "demo.content.importer.mesh",
      .member = &content::import::ImportConcurrency::mesh_build,
    },
    Pipeline {
      .label = "Geometry",
      .command = "demo.content.importer.geometry",
      .member = &content::import::ImportConcurrency::geometry,
    },
    Pipeline {
      .label = "Scene",
      .command = "demo.content.importer.scene",
      .member = &content::import::ImportConcurrency::scene,
    },
  };

  //! An output layout directory, its command and its layout member.
  struct LayoutField {
    const char* label;
    std::string_view command;
    std::string LooseCookedLayout::* member;
  };
  constexpr auto kLayoutFields = std::array {
    LayoutField {
      .label = "Virtual Root",
      .command = "demo.content.layout.virtual_root",
      .member = &LooseCookedLayout::virtual_mount_root,
    },
    LayoutField {
      .label = "Resources Dir",
      .command = "demo.content.layout.resources_dir",
      .member = &LooseCookedLayout::resources_dir,
    },
  };

  auto UpAxisLabel(const content::import::SourceUpAxis axis) -> std::string_view
  {
    switch (axis) {
    case content::import::SourceUpAxis::kFromFile:
      return "From file";
    case content::import::SourceUpAxis::kY:
      return "Y up";
    case content::import::SourceUpAxis::kZ:
      return "Z up";
    }
    return "Unknown";
  }

  auto MaxSizeLabel(const uint32_t size) -> std::string
  {
    if (size == 0U) {
      return "Source size (0)";
    }
    return std::to_string(size);
  }

  auto UpAxisLabels() -> std::vector<std::string>
  {
    std::vector<std::string> labels;
    labels.reserve(kUpAxes.size());
    for (const auto axis : kUpAxes) {
      labels.emplace_back(UpAxisLabel(axis));
    }
    return labels;
  }

  auto MaxSizeLabels() -> std::vector<std::string>
  {
    std::vector<std::string> labels;
    labels.reserve(kMaxSizes.size());
    for (const auto size : kMaxSizes) {
      labels.push_back(MaxSizeLabel(size));
    }
    return labels;
  }

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  template <typename EnumT, std::size_t N>
  auto EnumNames(const std::array<EnumT, N>& items) -> std::vector<std::string>
  {
    std::vector<std::string> names;
    names.reserve(N);
    for (const auto item : items) {
      names.emplace_back(nostd::to_string(item));
    }
    return names;
  }

  //! Registers a command selecting one of `items` by name.
  template <typename EnumT, std::size_t N>
  auto AddEnum(PanelCommands& commands, const std::string_view name,
    std::string label, std::string help, const std::array<EnumT, N>& items,
    std::function<void(EnumT)> set) -> void
  {
    commands.AddEnum(Name(name), std::move(label), std::move(help),
      EnumNames(items),
      [items, set = std::move(set)](
        const std::size_t i) -> void { set(items.at(i)); });
  }

  auto EditExplorer(observer_ptr<ContentVm> vm,
    const std::function<void(ContentExplorerSettings&)>& edit) -> void
  {
    auto settings = vm->GetExplorerSettings();
    edit(settings);
    vm->SetExplorerSettings(settings);
  }

  auto EditImport(observer_ptr<ContentVm> vm,
    const std::function<void(ImportOptions&)>& edit) -> void
  {
    auto options = vm->GetImportOptions();
    edit(options);
    vm->SetImportOptions(options);
  }

  auto EditTuning(observer_ptr<ContentVm> vm,
    const std::function<void(TextureTuning&)>& edit) -> void
  {
    auto tuning = vm->GetTextureTuning();
    edit(tuning);
    vm->SetTextureTuning(tuning);
  }

  auto EditService(observer_ptr<ContentVm> vm,
    const std::function<void(AsyncImportService::Config&)>& edit) -> void
  {
    auto config = vm->GetServiceConfig();
    edit(config);
    vm->SetServiceConfig(config);
  }

  auto SetFlag(ImportOptions& options, const ImportContentFlags flag,
    const bool enabled) -> void
  {
    options.import_content = enabled ? options.import_content | flag
                                     : options.import_content & ~flag;
  }

  auto HasFlag(const ImportOptions& options, const ImportContentFlags flag)
    -> bool
  {
    return (options.import_content & flag) != ImportContentFlags::kNone;
  }

  auto RegisterLibraryCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddText(Name(kModelRoot), "Model Root",
      "Folder scanned for importable sources",
      [vm](const std::string& path) -> void { vm->SetModelRoot(path); });
    commands.AddAction(Name(kRefreshSources), "Refresh Sources",
      "Rescan the model root", [vm] -> void { vm->RefreshSources(); });
    commands.AddText(Name(kImport), "Import Source",
      "Import a source file into a new generation",
      [vm](const std::string& path) -> void { vm->StartImport(path); });
    commands.AddAction(Name(kCancelImport), "Cancel Import",
      "Cancel the running import", [vm] -> void { vm->CancelActiveImport(); });
    commands.AddText(Name(kMountPak), "Mount PAK", "Mount a PAK file",
      [vm](const std::string& path) -> void { vm->MountPak(path); });
    commands.AddText(Name(kLoadLibrary), "Load Library",
      "Mount an import record (*.import.json) or a cooked index",
      [vm](const std::string& path) -> void { vm->LoadLibrary(path); });
    commands.AddAction(Name(kUnloadAll), "Unload Library",
      "Unmount every PAK and index", [vm] -> void { vm->UnloadAllLibrary(); });
    commands.AddAction(Name(kRefreshLibrary), "Refresh Library",
      "Rebuild the scene list from the mounted sources",
      [vm] -> void { vm->RefreshLibrary(); });
    commands.Add({
      .name = Name(kLoadScene),
      .label = "Load Scene",
      .help = "Load a library scene by asset key, from the source that lists "
              "it",
      .params = {
        commands::CommandParam {
          .name = "key",
          .type = commands::ParamType::kString,
          .help = "Asset key",
          .required = true,
        },
        commands::CommandParam {
          .name = "source",
          .type = commands::ParamType::kPath,
          .help = "PAK or index that lists the scene; empty takes the first",
          .default_value = commands::ParamValue { std::string {} },
        },
      },
      .handler = [vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        const auto& key = args.GetString("key");
        const auto& source = args.GetString("source");
        for (const auto& scene : vm->GetAvailableScenes()) {
          if (nostd::to_string(scene.key) == key
            && (source.empty() || scene.source.path.string() == source)) {
            vm->RequestSceneLoad(scene);
            return commands::CommandResult::Success();
          }
        }
        return commands::CommandResult::Failure(
          commands::ExecutionStatus::kInvalidArguments,
          "No library scene has that key");
      },
    });
    commands.AddAction(Name(kCancelSceneLoad), "Cancel Scene Load",
      "Cancel the scene being loaded", [vm] -> void { vm->CancelSceneLoad(); });
    commands.AddAction(Name(kClearDiagnostics), "Clear Import Diagnostics",
      "Clear the import diagnostics list",
      [vm] -> void { vm->ClearDiagnostics(); });
  }

  auto RegisterExplorerCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddBool(Name(kIncludeFbx), "List FBX Sources",
      "List FBX files under the model root", [vm](const bool v) -> void {
        EditExplorer(vm, [v](auto& s) -> void { s.include_fbx = v; });
      });
    commands.AddBool(Name(kIncludeGlb), "List GLB Sources",
      "List GLB files under the model root", [vm](const bool v) -> void {
        EditExplorer(vm, [v](auto& s) -> void { s.include_glb = v; });
      });
    commands.AddBool(Name(kIncludeGltf), "List glTF Sources",
      "List glTF files under the model root", [vm](const bool v) -> void {
        EditExplorer(vm, [v](auto& s) -> void { s.include_gltf = v; });
      });
    commands.AddBool(Name(kAutoLoad), "Load Scene After Import",
      "Load the imported scene when an import finishes",
      [vm](const bool v) -> void {
        EditExplorer(vm, [v](auto& s) -> void { s.auto_load_on_import = v; });
      });
  }

  auto RegisterImportCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    const auto flag = [&commands, vm](const std::string_view name,
                        std::string label, const ImportContentFlags f) -> void {
      commands.AddBool(Name(name), std::move(label),
        "Generate this content when importing", [vm, f](const bool v) -> void {
          EditImport(vm, [f, v](auto& o) -> void { SetFlag(o, f, v); });
        });
    };
    flag(kImportTextures, "Import Textures", ImportContentFlags::kTextures);
    flag(kImportMaterials, "Import Materials", ImportContentFlags::kMaterials);
    flag(kImportGeometry, "Import Geometry", ImportContentFlags::kGeometry);
    flag(kImportScene, "Import Scene", ImportContentFlags::kScene);
    commands.AddBool(Name(kHashing), "Content Hashing", "Hash imported content",
      [vm](const bool v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.with_content_hashing = v; });
      });
    commands.AddBool(Name(kIgnoreNonMesh), "Ignore Non-Mesh Primitives",
      "Skip points and lines when importing", [vm](const bool v) -> void {
        EditImport(
          vm, [v](auto& o) -> void { o.ignore_non_mesh_primitives = v; });
      });
    AddEnum<content::import::NodePruningPolicy>(commands, kNodePruning,
      "Node Pruning", "Which empty nodes the importer drops", kPruningModes,
      [vm](const auto v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.node_pruning = v; });
      });
    AddEnum<content::import::UnitNormalizationPolicy>(commands, kUnits,
      "Import Units", "How source units are converted", kUnitPolicies,
      [vm](const auto v) -> void {
        EditImport(
          vm, [v](auto& o) -> void { o.coordinate.unit_normalization = v; });
      });
    commands.AddFloat(
      Name(kUnitScale), "Import Unit Scale",
      "Scale factor of the custom unit policy",
      [vm](const float v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.coordinate.unit_scale = v; });
      },
      0.001F, 1000.0F);
    commands.AddEnum(Name(kUpAxis), "Up Axis", "Up axis of an FBX source",
      UpAxisLabels(), [vm](const std::size_t i) -> void {
        EditImport(vm, [axis = kUpAxes.at(i)](auto& o) -> void {
          o.coordinate.up_axis = axis;
        });
      });
    commands.AddFloat(
      Name(kSmoothingAngle), "Normals Smoothing Angle",
      "Largest angle in degrees between faces sharing a smoothed normal",
      [vm](const float v) -> void {
        EditImport(
          vm, [v](auto& o) -> void { o.normals_smoothing_angle_deg = v; });
      },
      0.0F, 180.0F);
    commands.AddBool(Name(kGenerateLods), "Generate LODs",
      "Generate LODs for geometries with one LOD", [vm](const bool v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.lod_generation.enabled = v; });
      });
    commands.AddInt(
      Name(kLodLevels), "LOD Levels", "LODs generated after LOD 0",
      [vm](const int v) -> void {
        EditImport(vm, [v](auto& o) -> void {
          o.lod_generation.levels = static_cast<uint32_t>(v);
        });
      },
      static_cast<int>(content::import::LodGenerationOptions::kMinLevels),
      static_cast<int>(content::import::LodGenerationOptions::kMaxLevels));
    commands.AddFloat(
      Name(kLodReduction), "LOD Reduction",
      "Triangles each generated LOD removes, in percent of the previous LOD's",
      [vm](const float v) -> void {
        EditImport(
          vm, [v](auto& o) -> void { o.lod_generation.reduction_percent = v; });
      },
      content::import::LodGenerationOptions::kMinReductionPercent,
      content::import::LodGenerationOptions::kMaxReductionPercent);
    commands.AddBool(Name(kImportLights), "Import Lights",
      "Import the source's lights", [vm](const bool v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.import_lights = v; });
      });
    commands.AddBool(Name(kImportCameras), "Import Cameras",
      "Import the source's cameras", [vm](const bool v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.import_cameras = v; });
      });
    commands.AddFloat(
      Name(kLightScale), "Light Intensity Scale",
      "Factor every imported light's intensity is multiplied by",
      [vm](const float v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.light_intensity_scale = v; });
      },
      0.0F, 1000.0F);
    AddEnum<content::import::GeometryAttributePolicy>(commands, kNormalPolicy,
      "Normal Policy", "How normals are imported", kGeometryPolicies,
      [vm](const auto v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.normal_policy = v; });
      });
    AddEnum<content::import::GeometryAttributePolicy>(commands, kTangentPolicy,
      "Tangent Policy", "How tangents are imported", kGeometryPolicies,
      [vm](const auto v) -> void {
        EditImport(vm, [v](auto& o) -> void { o.tangent_policy = v; });
      });
  }

  auto RegisterTextureCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    const auto toggle
      = [&commands, vm](const std::string_view name, std::string label,
          std::string help, bool TextureTuning::* member) -> void {
      commands.AddBool(Name(name), std::move(label), std::move(help),
        [vm, member](const bool v) -> void {
          EditTuning(vm, [member, v](auto& t) -> void { t.*member = v; });
        });
    };
    toggle(kTuning, "Texture Tuning", "Apply the texture tuning on import",
      &TextureTuning::enabled);
    toggle(kBakeHdr, "Bake HDR To LDR", "Tonemap HDR sources into LDR",
      &TextureTuning::bake_hdr_to_ldr);
    toggle(kFlipGreen, "Flip Normal Green", "Flip the normal map's Y channel",
      &TextureTuning::flip_normal_green);
    toggle(kRenormalize, "Renormalize Mips",
      "Renormalize normal map mip levels",
      &TextureTuning::renormalize_normals_in_mips);
    toggle(kCubemap, "Import As Cubemap", "Import the source as a cubemap",
      &TextureTuning::import_cubemap);
    toggle(kEquirect, "Equirect To Cubemap",
      "Convert an equirectangular source to a cubemap",
      &TextureTuning::equirect_to_cubemap);

    AddEnum<content::import::TextureIntent>(commands, kIntent, "Texture Intent",
      "What the texture is used for", kIntents, [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.intent = v; });
      });
    AddEnum<ColorSpace>(commands, kColorSpace, "Source Color Space",
      "Color space of the source image", kColorSpaces,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.source_color_space = v; });
      });
    AddEnum<content::import::MipPolicy>(commands, kMipPolicy, "Mip Policy",
      "Which mip levels are generated", kMipPolicies,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.mip_policy = v; });
      });
    commands.AddInt(
      Name(kMaxMips), "Max Mips", "Mip levels of the max-count policy",
      [vm](const int v) -> void {
        EditTuning(vm,
          [v](auto& t) -> void { t.max_mip_levels = static_cast<uint8_t>(v); });
      },
      1, 16);
    commands.AddEnum(Name(kMaxSize), "Max Texture Size",
      "Largest width or height of a stored texture", MaxSizeLabels(),
      [vm](const std::size_t i) -> void {
        EditTuning(
          vm, [size = kMaxSizes.at(i)](auto& t) -> void { t.max_size = size; });
      });
    AddEnum<content::import::MipFilter>(commands, kMipFilter, "Mip Filter",
      "Downsampling filter", kMipFilters, [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.mip_filter = v; });
      });
    AddEnum<Format>(commands, kColorFormat, "Color Format",
      "Output format of color textures", kFormats, [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.color_output_format = v; });
      });
    AddEnum<Format>(commands, kDataFormat, "Data Format",
      "Output format of data textures", kFormats, [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.data_output_format = v; });
      });
    AddEnum<content::import::Bc7Quality>(commands, kBc7Quality, "BC7 Quality",
      "BC7 encoder quality", kBc7Tiers, [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.bc7_quality = v; });
      });
    AddEnum<content::import::HdrHandling>(commands, kHdrHandling,
      "HDR Handling", "What happens to HDR sources", kHdrModes,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.hdr_handling = v; });
      });
    commands.AddFloat(
      Name(kExposureEv), "HDR Bake Exposure",
      "Exposure in EV when baking HDR to LDR",
      [vm](const float v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.exposure_ev = v; });
      },
      -10.0F, 10.0F);
    commands.AddInt(
      Name(kFaceSize), "Cubemap Face Size",
      "Face size in pixels; 0 derives it from the source",
      [vm](const int v) -> void {
        EditTuning(vm, [v](auto& t) -> void {
          t.cubemap_face_size = static_cast<uint32_t>(v);
        });
      },
      0, 8192);
    AddEnum<content::import::CubeMapImageLayout>(commands, kCubeLayout,
      "Cube Layout", "Face layout of a cubemap source", kCubeLayouts,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.cubemap_layout = v; });
      });
  }

  auto RegisterImporterCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddInt(
      Name(kThreadPool), "Importer Threads",
      "Size of the importer's thread pool; takes effect on restart",
      [vm](const int v) -> void {
        EditService(vm, [v](auto& c) -> void {
          c.thread_pool_size = static_cast<uint32_t>(v);
        });
      },
      1, 128);
    for (const auto& pipeline : kPipelines) {
      const auto member = pipeline.member;
      commands.AddInt(
        std::string(pipeline.command) + ".workers",
        std::string(pipeline.label) + " Workers",
        "Workers of the pipeline; takes effect on restart",
        [vm, member](const int v) -> void {
          EditService(vm, [member, v](auto& c) -> void {
            (c.concurrency.*member).workers = static_cast<uint32_t>(v);
          });
        },
        1, 64);
      commands.AddInt(
        std::string(pipeline.command) + ".queue",
        std::string(pipeline.label) + " Queue",
        "Queue capacity of the pipeline; takes effect on restart",
        [vm, member](const int v) -> void {
          EditService(vm, [member, v](auto& c) -> void {
            (c.concurrency.*member).queue_capacity = static_cast<uint32_t>(v);
          });
        },
        1, 256);
    }
    commands.AddAction(Name(kRestartImporter), "Restart Importer",
      "Restart the import service with its current settings",
      [vm] -> void { vm->RestartImportService(); });
    for (const auto& field : kLayoutFields) {
      const auto member = field.member;
      commands.AddText(std::string(field.command), field.label,
        "Output layout of new generations",
        [vm, member](const std::string& value) -> void {
          auto layout = vm->GetLayout();
          layout.*member = value;
          vm->SetLayout(layout);
        });
    }
  }

  auto HelpMarker(const char* description) -> void
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
      ImGui::TextUnformatted(description);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }

  //! A text field whose value is committed when it loses focus; true then.
  auto CommittedTextField(const char* label, std::string& value) -> bool
  {
    std::array<char, 1024> buffer {};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    ImGui::InputText(label, buffer.data(), buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      value = buffer.data();
      return true;
    }
    return false;
  }

  //! Draws a combo of `items` and posts `name` with the one picked.
  template <typename EnumT, std::size_t N>
  auto EnumCombo(const PanelCommands& commands, const std::string_view name,
    const char* label, const EnumT value, const std::array<EnumT, N>& items)
    -> void
  {
    const std::string preview(nostd::to_string(value));
    if (!ImGui::BeginCombo(label, preview.c_str())) {
      return;
    }
    for (const auto candidate : items) {
      const bool is_selected = candidate == value;
      const std::string item(nostd::to_string(candidate));
      if (ImGui::Selectable(item.c_str(), is_selected) && !is_selected) {
        commands.RunText(name, item);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  //! Draws a combo of labeled choices and posts `name` with the one picked.
  auto LabeledCombo(const PanelCommands& commands, const std::string_view name,
    const char* label, const std::string& current,
    const std::vector<std::string>& items) -> void
  {
    if (!ImGui::BeginCombo(label, current.c_str())) {
      return;
    }
    for (const auto& item : items) {
      const bool is_selected = item == current;
      if (ImGui::Selectable(item.c_str(), is_selected) && !is_selected) {
        commands.RunText(name, item);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  auto Checkbox(const PanelCommands& commands, const std::string_view name,
    const char* label, bool value) -> void
  {
    if (ImGui::Checkbox(label, &value)) {
      commands.RunBool(name, value);
    }
  }

  auto SceneSourceLabel(const SceneEntry& entry) -> std::string
  {
    const auto file_name = entry.source.path.filename().string();
    if (entry.source.kind == SceneSourceKind::kPak) {
      return fmt::format("PAK: {}", file_name);
    }
    return fmt::format("Index: {}", file_name);
  }

} // namespace

ContentLoaderPanel::ContentLoaderPanel(
  observer_ptr<ContentVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "ContentLoaderPanel requires ContentVm");
  DCHECK_NOTNULL_F(commands_, "ContentLoaderPanel requires PanelCommands");
  RegisterLibraryCommands(*commands_, vm_);
  RegisterExplorerCommands(*commands_, vm_);
  RegisterImportCommands(*commands_, vm_);
  RegisterTextureCommands(*commands_, vm_);
  RegisterImporterCommands(*commands_, vm_);
}

auto ContentLoaderPanel::DrawContents() -> void
{
  RunBrowseSelection();

  const bool is_importing = vm_->IsImportInProgress();
  const bool is_scene_loading = vm_->IsSceneLoading();
  const bool show_progress
    = is_importing || is_scene_loading || vm_->ShouldShowSceneLoadProgress();

  const auto& style = ImGui::GetStyle();
  const float status_height = ImGui::GetFrameHeight() + style.ItemSpacing.y;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float main_height = std::max(0.0F, avail.y - status_height);

  // Only the status area stays interactive while an operation is in flight.
  ImGui::BeginDisabled(is_importing || is_scene_loading);
  ImGui::PushStyleVar(
    ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0F));
  if (ImGui::BeginChild("ContentLoaderMain", ImVec2(0.0F, main_height))
    && ImGui::BeginTabBar("ContentLoaderTabs", ImGuiTabBarFlags_None)) {
    if (ImGui::BeginTabItem("Sources")) {
      DrawSourcesSection();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Library")) {
      DrawLibrarySection();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Diagnostics")) {
      DrawDiagnosticsSection();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Advanced")) {
      DrawAdvancedSection();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::EndChild();
  ImGui::EndDisabled();

  if (ImGui::BeginChild("ContentLoaderStatus", ImVec2(0.0F, status_height), 0,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    if (show_progress) {
      DrawProgress(is_importing, is_scene_loading);
    } else {
      ImGui::TextDisabled("Ready");
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

auto ContentLoaderPanel::RunBrowseSelection() -> void
{
  const auto selection = vm_->ConsumeBrowseSelection();
  if (!selection) {
    return;
  }
  const auto path = selection->path.string();
  switch (selection->target) {
  case ContentVm::BrowseTarget::kModelRoot:
    commands_->RunText(kModelRoot, path);
    break;
  case ContentVm::BrowseTarget::kSourceFile:
    commands_->RunText(kImport, path);
    break;
  case ContentVm::BrowseTarget::kPakFile:
    commands_->RunText(kMountPak, path);
    break;
  case ContentVm::BrowseTarget::kLibraryFile:
    commands_->RunText(kLoadLibrary, path);
    break;
  }
}

auto ContentLoaderPanel::DrawProgress(
  const bool is_importing, const bool is_scene_loading) -> void
{
  const float progress = is_importing ? vm_->GetActiveImportProgress()
                                      : vm_->GetSceneLoadProgress();
  const std::string message
    = is_importing ? vm_->GetActiveImportMessage() : vm_->GetSceneLoadMessage();

  const ImVec4 fill_color = is_importing ? ImVec4(0.2F, 0.7F, 0.4F, 1.0F)
                                         : ImVec4(0.2F, 0.5F, 0.85F, 1.0F);
  const ImVec4 frame_color = is_importing ? ImVec4(0.1F, 0.3F, 0.2F, 1.0F)
                                          : ImVec4(0.1F, 0.2F, 0.35F, 1.0F);

  const char* cancel_label = nullptr;
  if (is_importing) {
    cancel_label = "Cancel Import";
  } else if (is_scene_loading) {
    cancel_label = "Cancel Scene Load";
  }

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, frame_color);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fill_color);

  float button_width = 0.0F;
  if (cancel_label != nullptr) {
    const ImVec2 label_size = ImGui::CalcTextSize(cancel_label);
    button_width = label_size.x + (ImGui::GetStyle().FramePadding.x * 2.0F);
  }
  const float progress_width = ImGui::GetContentRegionAvail().x
    - (cancel_label != nullptr ? button_width + ImGui::GetStyle().ItemSpacing.x
                               : 0.0F);

  ImGui::ProgressBar(progress, ImVec2(progress_width, 0.0F), message.c_str());
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();

  if (cancel_label != nullptr) {
    ImGui::SameLine();
    if (ImGui::Button(cancel_label)) {
      commands_->Run(is_importing ? kCancelImport : kCancelSceneLoad);
    }
  }
}

auto ContentLoaderPanel::DrawSourcesSection() -> void
{
  DrawWorkflowSettings();
  ImGui::Spacing();
  DrawImportSettings();
  ImGui::Spacing();
  DrawTextureTuningSettings();

  const auto explorer = vm_->GetExplorerSettings();

  ImGui::Dummy({ 0, 4 });
  if (ImGui::CollapsingHeader(
        "Source Folder", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    std::string root_path = explorer.model_root.string();
    const float button_width = ImGui::CalcTextSize(ICON_FA_FOLDER " Browse").x
      + (ImGui::GetStyle().FramePadding.x * 2.0F);
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float input_width = std::max(
      0.0F, available_width - button_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushItemWidth(input_width);
    if (CommittedTextField("##model_root", root_path)) {
      commands_->RunText(kModelRoot, root_path);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 0.0F, 1.0F));
    if (ImGui::Button(ICON_FA_FOLDER " Browse##root")) {
      vm_->BrowseForModelRoot();
    }
    ImGui::PopStyleColor();

    Checkbox(*commands_, kIncludeFbx, "FBX", explorer.include_fbx);
    ImGui::SameLine();
    Checkbox(*commands_, kIncludeGlb, "GLB", explorer.include_glb);
    ImGui::SameLine();
    Checkbox(*commands_, kIncludeGltf, "GLTF", explorer.include_gltf);
    ImGui::Unindent();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Discovery");

  if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##refresh_sources")) {
    commands_->Run(kRefreshSources);
  }
  ImGui::SameLine();
  ImGui::InputTextWithHint("##SourceFilter", "Filter sources...",
    source_filter_.data(), source_filter_.size());
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_FILE " Select File##browse_file")) {
    vm_->BrowseForSourceFile();
  }

  ImGui::Dummy(ImVec2(0, 20));

  const auto& sources = vm_->GetSources();
  if (sources.empty()) {
    ImGui::TextDisabled("No sources found. Check your Model Root.");
  }

  const std::string_view filter(source_filter_.data());
  if (ImGui::BeginChild("SourcesList", ImVec2(0, 0), 1)) {
    for (const auto& src : sources) {
      const std::string filename = src.path.filename().string();
      if (!filter.empty() && !filename.contains(filter)) {
        continue;
      }

      if (ImGui::Selectable(filename.c_str())) {
        commands_->RunText(kImport, src.path.string());
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", src.path.string().c_str());
      }
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawLibrarySection() -> void
{
  ImGui::SeparatorText("Mount Management");
  if (ImGui::Button(ICON_FA_FILE " Select PAK##select_pak")) {
    vm_->BrowseForPak();
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_FILE " Select Library##select_index")) {
    vm_->BrowseForLibrary();
  }
  ImGui::SameLine();
  if (ImGui::Button("Unload All")) {
    commands_->Run(kUnloadAll);
  }

  ImGui::Dummy(ImVec2(0, 20));

  if (ImGui::TreeNode("Mounted Items")) {
    for (const auto& pak : vm_->GetLoadedPaks()) {
      ImGui::BulletText("PAK: %s", pak.filename().string().c_str());
    }
    for (const auto& idx : vm_->GetLoadedIndices()) {
      ImGui::BulletText("Index: %s", idx.filename().string().c_str());
    }
    ImGui::TreePop();
  }

  ImGui::Dummy(ImVec2(0, 20));

  ImGui::SeparatorText("Library Scenes");
  if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##refresh_scenes")) {
    commands_->Run(kRefreshLibrary);
  }
  ImGui::SameLine();
  ImGui::InputTextWithHint("##SceneFilter", "Search scenes...",
    scene_filter_.data(), scene_filter_.size());

  ImGui::Dummy(ImVec2(0, 20));

  const std::string_view filter(scene_filter_.data());
  if (ImGui::BeginChild("LibraryScenes", ImVec2(0, 0), 1)) {
    for (const auto& scene : vm_->GetAvailableScenes()) {
      if (!filter.empty() && !scene.name.contains(filter)) {
        continue;
      }

      const auto key = std::string(nostd::to_string(scene.key));
      const auto source_label = SceneSourceLabel(scene);
      const auto label = fmt::format("{} ({})##{}-{}", scene.name, source_label,
        key, scene.source.path.string());
      if (ImGui::Selectable(label.c_str())) {
        commands_->Run(kLoadScene,
          { { "key", key }, { "source", scene.source.path.string() } });
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
          "Virtual Path: %s\nKey: %s\nSource: %s\nSource Path: %s",
          scene.name.c_str(), key.c_str(), source_label.c_str(),
          scene.source.path.string().c_str());
      }
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawDiagnosticsSection() -> void
{
  if (ImGui::Button("Clear All")) {
    commands_->Run(kClearDiagnostics);
  }
  ImGui::Spacing();

  if (ImGui::BeginChild("DiagnosticsList", ImVec2(0, 0), 1)) {
    for (const auto& diag : vm_->GetDiagnostics()) {
      auto color = ImVec4(0.8F, 0.8F, 0.8F, 1);

      if (diag.severity == content::import::ImportSeverity::kError) {
        color = ImVec4(1, 0.4F, 0.4F, 1);
      } else if (diag.severity == content::import::ImportSeverity::kWarning) {
        color = ImVec4(1, 0.8F, 0.4F, 1);
      }

      ImGui::TextColored(color, "[%s] %s: %s",
        std::string(nostd::to_string(diag.severity)).c_str(), diag.code.c_str(),
        diag.message.c_str());
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawWorkflowSettings() -> void
{
  if (ImGui::CollapsingHeader("Workflow Settings")) {
    Checkbox(*commands_, kAutoLoad, "Auto-load scene after import",
      vm_->GetExplorerSettings().auto_load_on_import);
  }
}

auto ContentLoaderPanel::DrawImportSettings() -> void
{
  if (!ImGui::CollapsingHeader("Import Configuration")) {
    return;
  }
  const auto options = vm_->GetImportOptions();
  if (ImGui::TreeNodeEx("Content Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
    Checkbox(*commands_, kImportTextures, "Textures",
      HasFlag(options, ImportContentFlags::kTextures));
    ImGui::SameLine();
    Checkbox(*commands_, kImportMaterials, "Materials",
      HasFlag(options, ImportContentFlags::kMaterials));
    ImGui::SameLine();
    Checkbox(*commands_, kImportGeometry, "Geometry",
      HasFlag(options, ImportContentFlags::kGeometry));
    ImGui::SameLine();
    Checkbox(*commands_, kImportScene, "Scene",
      HasFlag(options, ImportContentFlags::kScene));
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
    Checkbox(
      *commands_, kHashing, "Enable Hashing", options.with_content_hashing);
    Checkbox(*commands_, kIgnoreNonMesh, "Ignore Non-Mesh Primitives",
      options.ignore_non_mesh_primitives);
    EnumCombo(*commands_, kNodePruning, "Node Pruning", options.node_pruning,
      kPruningModes);
    EnumCombo(*commands_, kUnits, "Units",
      options.coordinate.unit_normalization, kUnitPolicies);
    if (options.coordinate.unit_normalization
      == content::import::UnitNormalizationPolicy::kApplyCustomFactor) {
      float scale = options.coordinate.unit_scale;
      if (ImGui::DragFloat("Scale Factor", &scale, 0.1F, 0.001F, 1000.0F)) {
        commands_->RunFloat(kUnitScale, scale);
      }
    }
    LabeledCombo(*commands_, kUpAxis, "Up Axis",
      std::string(UpAxisLabel(options.coordinate.up_axis)), UpAxisLabels());
    float smoothing = options.normals_smoothing_angle_deg;
    if (ImGui::DragFloat("Smoothing Angle", &smoothing, 1.0F, 0.0F, 180.0F)) {
      commands_->RunFloat(kSmoothingAngle, smoothing);
    }
    Checkbox(*commands_, kGenerateLods, "Generate LODs",
      options.lod_generation.enabled);
    if (options.lod_generation.enabled) {
      using content::import::LodGenerationOptions;
      int levels = static_cast<int>(options.lod_generation.levels);
      if (ImGui::SliderInt("LOD Levels", &levels,
            static_cast<int>(LodGenerationOptions::kMinLevels),
            static_cast<int>(LodGenerationOptions::kMaxLevels))) {
        commands_->RunInt(kLodLevels, levels);
      }
      float reduction = options.lod_generation.reduction_percent;
      if (ImGui::DragFloat("Reduction per LOD (%)", &reduction, 1.0F,
            LodGenerationOptions::kMinReductionPercent,
            LodGenerationOptions::kMaxReductionPercent)) {
        commands_->RunFloat(kLodReduction, reduction);
      }
    }
    EnumCombo(*commands_, kNormalPolicy, "Normal Policy", options.normal_policy,
      kGeometryPolicies);
    EnumCombo(*commands_, kTangentPolicy, "Tangent Policy",
      options.tangent_policy, kGeometryPolicies);
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Lights and Cameras", ImGuiTreeNodeFlags_DefaultOpen)) {
    Checkbox(*commands_, kImportLights, "Import Lights", options.import_lights);
    ImGui::SameLine();
    Checkbox(
      *commands_, kImportCameras, "Import Cameras", options.import_cameras);
    float scale = options.light_intensity_scale;
    if (ImGui::DragFloat(
          "Light Intensity Scale", &scale, 0.05F, 0.0F, 1000.0F)) {
      commands_->RunFloat(kLightScale, scale);
    }
    ImGui::TreePop();
  }
}

auto ContentLoaderPanel::DrawTextureTuningSettings() -> void
{
  if (!ImGui::CollapsingHeader("Texture Tuning")) {
    return;
  }
  const auto tuning = vm_->GetTextureTuning();

  // The size limit applies whether or not tuning is enabled.
  LabeledCombo(*commands_, kMaxSize, "Max Size", MaxSizeLabel(tuning.max_size),
    MaxSizeLabels());
  Checkbox(*commands_, kTuning, "Enabled", tuning.enabled);
  EnumCombo(*commands_, kIntent, "Intent", tuning.intent, kIntents);
  EnumCombo(*commands_, kColorSpace, "Source Color Space",
    tuning.source_color_space, kColorSpaces);
  EnumCombo(
    *commands_, kMipPolicy, "Mip Policy", tuning.mip_policy, kMipPolicies);
  if (tuning.mip_policy == content::import::MipPolicy::kMaxCount) {
    int max_mips = tuning.max_mip_levels;
    if (ImGui::SliderInt("Max Mips", &max_mips, 1, 16)) {
      commands_->RunInt(kMaxMips, max_mips);
    }
  }
  EnumCombo(
    *commands_, kMipFilter, "Mip Filter", tuning.mip_filter, kMipFilters);
  EnumCombo(*commands_, kColorFormat, "Color Format",
    tuning.color_output_format, kFormats);
  EnumCombo(*commands_, kDataFormat, "Data Format", tuning.data_output_format,
    kFormats);
  EnumCombo(
    *commands_, kBc7Quality, "BC7 Quality", tuning.bc7_quality, kBc7Tiers);
  EnumCombo(
    *commands_, kHdrHandling, "HDR Handling", tuning.hdr_handling, kHdrModes);

  if (tuning.hdr_handling != content::import::HdrHandling::kKeepFloat) {
    Checkbox(*commands_, kBakeHdr, "Bake HDR to LDR", tuning.bake_hdr_to_ldr);
    if (tuning.bake_hdr_to_ldr) {
      ImGui::Indent();
      float exposure = tuning.exposure_ev;
      if (ImGui::DragFloat("Exposure (EV)", &exposure, 0.1F, -10.0F, 10.0F)) {
        commands_->RunFloat(kExposureEv, exposure);
      }
      ImGui::Unindent();
    }
  }

  ImGui::Separator();
  Checkbox(*commands_, kFlipGreen, "Flip Green Channel (Normal)",
    tuning.flip_normal_green);
  Checkbox(*commands_, kRenormalize, "Renormalize Mips",
    tuning.renormalize_normals_in_mips);

  ImGui::Separator();
  Checkbox(*commands_, kCubemap, "Import as Cubemap", tuning.import_cubemap);
  if (tuning.import_cubemap) {
    ImGui::Indent();
    Checkbox(
      *commands_, kEquirect, "Equirect to Cubemap", tuning.equirect_to_cubemap);
    if (tuning.equirect_to_cubemap) {
      int face_size = static_cast<int>(tuning.cubemap_face_size);
      if (ImGui::DragInt("Face Size", &face_size, 256, 0, 8192)) {
        commands_->RunInt(kFaceSize, face_size);
      }
    }
    EnumCombo(*commands_, kCubeLayout, "Cube Layout", tuning.cubemap_layout,
      kCubeLayouts);
    ImGui::Unindent();
  }
}

auto ContentLoaderPanel::DrawAdvancedSection() -> void
{
  if (ImGui::CollapsingHeader("Importer", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawImporterSettings();
  }
  if (ImGui::CollapsingHeader("Output Layout")) {
    DrawOutputLayout();
  }

  ImGui::Separator();
  if (ImGui::Button("Force Trim Asset Caches")) {
    commands_->Run(kTrimCache);
  }
  HelpMarker("Trims engine-side asset caches without unmounting content "
             "sources or changing the active scene.");
}

auto ContentLoaderPanel::DrawImporterSettings() -> void
{
  const auto config = vm_->GetServiceConfig();
  bool changed = false;

  ImGui::SeparatorText("Global Thread Pool");
  int pool = static_cast<int>(config.thread_pool_size);
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::DragInt("##global_threads", &pool, 0.1F, 1, 128, "Size: %d")) {
    commands_->RunInt(kThreadPool, pool);
    changed = true;
  }

  ImGui::SeparatorText("Pipeline Concurrency");
  for (const auto& pipeline : kPipelines) {
    const auto& pipe = config.concurrency.*pipeline.member;
    ImGui::PushID(pipeline.label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(pipeline.label);
    ImGui::SameLine(100);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float item_width
      = (ImGui::GetContentRegionAvail().x - spacing) / 2.0F;
    ImGui::SetNextItemWidth(item_width);
    int workers = static_cast<int>(pipe.workers);
    if (ImGui::DragInt("##Workers", &workers, 0.1F, 1, 64, "W: %d")) {
      commands_->RunInt(std::string(pipeline.command) + ".workers", workers);
      changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(item_width);
    int queue = static_cast<int>(pipe.queue_capacity);
    if (ImGui::DragInt("##Queue", &queue, 1.0F, 1, 256, "Q: %d")) {
      commands_->RunInt(std::string(pipeline.command) + ".queue", queue);
      changed = true;
    }
    ImGui::PopID();
  }

  importer_restart_pending_ = importer_restart_pending_ || changed;
  if (importer_restart_pending_) {
    ImGui::TextColored(
      ImVec4(1, 0.5F, 0, 1), "Changes take effect when the importer restarts.");
    if (ImGui::Button("Restart Importer")) {
      commands_->Run(kRestartImporter);
      importer_restart_pending_ = false;
    }
  }
}

auto ContentLoaderPanel::DrawOutputLayout() -> void
{
  ImGui::TextWrapped(
    "Generated storage: %s", vm_->GetGeneratedStorageRoot().c_str());
  ImGui::TextUnformatted(
    "Each import publishes an immutable source generation.");
  auto layout = vm_->GetLayout();

  ImGui::PushID("OutputLayoutTable");
  if (ImGui::BeginTable(
        "##OutputLayoutTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& field : kLayoutFields) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(field.label);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      auto& value = layout.*field.member;
      if (CommittedTextField(
            (std::string("##") + field.label).c_str(), value)) {
        commands_->RunText(field.command, value);
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopID();
}

auto ContentLoaderPanel::GetName() const noexcept -> std::string_view
{
  return "Content Loader";
}

auto ContentLoaderPanel::GetPreferredWidth() const noexcept -> float
{
  return 520.0F;
}

auto ContentLoaderPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconContentLoader;
}

auto ContentLoaderPanel::OnLoaded() -> void { vm_->RefreshSources(); }

auto ContentLoaderPanel::OnUnloaded() -> void { vm_->PersistLibraryState(); }

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
