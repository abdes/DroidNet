//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentVm.h"

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  using content::import::ImportContentFlags;
  using content::import::ImportOptions;
  using TextureTuning = ImportOptions::TextureTuning;

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  auto EditProfile(observer_ptr<ContentVm> vm,
    const std::function<void(ImportProfile&)>& edit) -> void
  {
    auto profile = vm->GetImportProfile();
    edit(profile);
    vm->SetImportProfile(profile);
  }

  auto EditOptions(observer_ptr<ContentVm> vm,
    const std::function<void(ImportOptions&)>& edit) -> void
  {
    EditProfile(vm, [&edit](ImportProfile& p) -> void { edit(p.options); });
  }

  auto EditTuning(observer_ptr<ContentVm> vm,
    const std::function<void(TextureTuning&)>& edit) -> void
  {
    EditProfile(vm, [&edit](ImportProfile& p) -> void { edit(p.tuning); });
  }

  auto EditTab(observer_ptr<ContentVm> vm,
    const std::function<void(ContentImportTabSettings&)>& edit) -> void
  {
    auto tab = vm->GetImportTab();
    edit(tab);
    vm->SetImportTab(tab);
  }

  //! Registers a command selecting one of `choices` by its label.
  template <typename EnumT, std::size_t N>
  auto AddChoice(PanelCommands& commands, const std::string_view name,
    std::string label, std::string help,
    const std::array<Choice<EnumT>, N>& choices, std::function<void(EnumT)> set)
    -> void
  {
    auto names = std::vector<std::string> {};
    names.reserve(N);
    for (const auto& choice : choices) {
      names.emplace_back(nostd::to_string(choice));
    }
    commands.AddEnum(Name(name), std::move(label), std::move(help),
      std::move(names),
      [choices, set = std::move(set)](
        const std::size_t i) -> void { set(choices.at(i).value); });
  }

  auto RegisterMountCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddText(Name(cc::kAddPak), "Add Pak",
      "Mount a pak on top of the added sources",
      [vm](const std::string& path) -> void {
        vm->AddSource({ .kind = data::CookedSourceKind::kPak, .path = path });
      });
    commands.AddText(Name(cc::kAddCookedRoot), "Add Cooked Root",
      "Mount a loose cooked root on top of the added sources",
      [vm](const std::string& path) -> void {
        vm->AddSource(
          { .kind = data::CookedSourceKind::kLooseCooked, .path = path });
      });
    commands.AddText(Name(cc::kAddSourceFolder), "Add Source Folder",
      "Add a folder of models to import, as a source mount of this machine",
      [vm](const std::string& path) -> void {
        static_cast<void>(vm->AddSourceFolder(path));
      });
    commands.AddText(Name(cc::kRemoveSourceFolder), "Remove Source Folder",
      "Remove a source folder the panel added, by its mount name",
      [vm](const std::string& name) -> void { vm->RemoveSourceFolder(name); });
    commands.AddInt(
      Name(cc::kRemoveSource), "Remove Source",
      "Remove a row of the mount list, by its index from the top",
      [vm](const int row) -> void {
        vm->RemoveSource(static_cast<std::size_t>(row));
      },
      0);
    commands.Add({
      .name = Name(cc::kMoveSource),
      .label = "Move Source",
      .help = "Move an added source to another row of the mount list",
      .params = {
        commands::CommandParam {
          .name = "from",
          .type = commands::ParamType::kInt,
          .help = "Row of the source, from the top",
          .required = true,
        },
        commands::CommandParam {
          .name = "to",
          .type = commands::ParamType::kInt,
          .help = "Row it moves to",
          .required = true,
        },
      },
      .handler = [vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        vm->MoveSource({
          .from = static_cast<std::size_t>(args.GetInt("from")),
          .to = static_cast<std::size_t>(args.GetInt("to")),
        });
        return commands::CommandResult::Success();
      },
    });
    commands.AddAction(Name(cc::kRemount), "Remount",
      "Apply the mount list again, retrying refused and missing sources",
      [vm] -> void { vm->Remount(); });
  }

  auto RegisterSceneCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.Add({
      .name = Name(cc::kLoadScene),
      .label = "Load Scene",
      .help = "Load a mounted scene by asset key or virtual path",
      .params = {
        commands::CommandParam {
          .name = "scene",
          .type = commands::ParamType::kString,
          .help = "Asset key or virtual path",
          .required = true,
        },
      },
      .handler = [vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        const auto& wanted = args.GetString("scene");
        for (const auto& group : vm->Scenes().Groups()) {
          for (const auto& scene : group.scenes) {
            if (nostd::to_string(scene.key) == wanted
              || scene.virtual_path == wanted) {
              vm->RequestSceneLoad(scene);
              return commands::CommandResult::Success();
            }
          }
        }
        return commands::CommandResult::Failure(
          commands::ExecutionStatus::kInvalidArguments,
          "No mounted scene has that key or virtual path");
      },
    });
    commands.AddAction(Name(cc::kReloadScene), "Reload Scene",
      "Load the loaded scene again", [vm] -> void { vm->ReloadScene(); });
    commands.AddAction(Name(cc::kUnloadScene), "Unload Scene",
      "Unload the loaded scene", [vm] -> void { vm->UnloadScene(); });
    commands.AddAction(Name(cc::kCancelSceneLoad), "Cancel Scene Load",
      "Cancel the scene being loaded", [vm] -> void { vm->CancelSceneLoad(); });
    commands.AddAction(Name(cc::kClearLog), "Clear Content Log",
      "Clear the content panel's log", [vm] -> void { vm->ClearLog(); });
  }

  auto RegisterImportTabCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddText(Name(cc::kImport), "Import Model",
      "Import a model file with the panel's settings",
      [vm](const std::string& path) -> void { vm->StartImport(path); });
    commands.AddText(Name(cc::kImportAsIs), "Reimport As Is",
      "Import a model file with its record's settings",
      [vm](const std::string& path) -> void {
        vm->StartImport(path, ContentVm::ImportSettingsSource::kRecord);
      });
    commands.AddText(Name(cc::kImportFile), "Import Model File",
      "Import a model file from anywhere, adding its folder as a source "
      "folder when no source folder holds it",
      [vm](const std::string& path) -> void { vm->ImportFile(path); });
    commands.AddAction(Name(cc::kCancelImport), "Cancel Import",
      "Cancel the running import", [vm] -> void { vm->CancelActiveImport(); });
    commands.AddAction(Name(cc::kRefreshModels), "Refresh Models",
      "List the source mount's models again",
      [vm] -> void { vm->RefreshModels(); });
    commands.AddText(Name(cc::kSourceMount), "Source Mount",
      "The source mount whose models the Import tab lists",
      [vm](const std::string& mount) -> void {
        EditTab(vm, [&mount](auto& tab) -> void { tab.source_mount = mount; });
      });
    commands.AddText(Name(cc::kModelFilter), "Model Filter",
      "Text the listed models' paths contain",
      [vm](const std::string& filter) -> void {
        EditTab(vm, [&filter](auto& tab) -> void { tab.filter = filter; });
      });
    const auto format
      = [&commands, vm](const std::string_view name, std::string label,
          bool ContentImportTabSettings::* member) -> void {
      commands.AddBool(Name(name), std::move(label),
        "List models of this format", [vm, member](const bool v) -> void {
          EditTab(vm, [member, v](auto& tab) -> void { tab.*member = v; });
        });
    };
    format(cc::kListFbx, "List FBX", &ContentImportTabSettings::fbx);
    format(cc::kListGlb, "List GLB", &ContentImportTabSettings::glb);
    format(cc::kListGltf, "List glTF", &ContentImportTabSettings::gltf);
    commands.AddBool(Name(cc::kLoadAfterImport), "Load Scene After Import",
      "Load the imported scene when an import finishes",
      [vm](const bool v) -> void {
        EditTab(
          vm, [v](auto& tab) -> void { tab.load_scene_after_import = v; });
      });
    auto groups = std::vector<std::string> {};
    for (const auto group : {
           ImportSettingsGroup::kContent,
           ImportSettingsGroup::kGeometry,
           ImportSettingsGroup::kLightsAndCameras,
           ImportSettingsGroup::kTextures,
           ImportSettingsGroup::kImporter,
         }) {
      groups.emplace_back(to_string(group));
    }
    commands.AddEnum(Name(cc::kResetGroup), "Reset Import Settings",
      "Put a group of import settings back to its defaults", std::move(groups),
      [vm](const std::size_t i) -> void {
        vm->ResetImportGroup(static_cast<ImportSettingsGroup>(i));
      });
  }

  auto RegisterContentOptionCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    const auto flag = [&commands, vm](const std::string_view name,
                        std::string label, const ImportContentFlags f) -> void {
      commands.AddBool(Name(name), std::move(label),
        "Generate this content when importing", [vm, f](const bool v) -> void {
          EditOptions(vm, [f, v](auto& o) -> void {
            o.import_content = v ? o.import_content | f : o.import_content & ~f;
          });
        });
    };
    flag(cc::kImportTextures, "Import Textures", ImportContentFlags::kTextures);
    flag(
      cc::kImportMaterials, "Import Materials", ImportContentFlags::kMaterials);
    flag(cc::kImportGeometry, "Import Geometry", ImportContentFlags::kGeometry);
    flag(cc::kImportScene, "Import Scene", ImportContentFlags::kScene);
    AddChoice<content::import::NodePruningPolicy>(commands, cc::kNodePruning,
      "Node Pruning", "Which empty nodes the importer drops",
      choices::kNodePruning, [vm](const auto v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.node_pruning = v; });
      });
  }

  auto RegisterGeometryCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    using content::import::LodGenerationOptions;
    AddChoice<content::import::UnitNormalizationPolicy>(commands, cc::kUnits,
      "Import Units", "How source units are converted", choices::kUnits,
      [vm](const auto v) -> void {
        EditOptions(
          vm, [v](auto& o) -> void { o.coordinate.unit_normalization = v; });
      });
    commands.AddFloat(
      Name(cc::kUnitScale), "Import Unit Scale",
      "Scale factor of the custom unit policy",
      [vm](const float v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.coordinate.unit_scale = v; });
      },
      0.001F, 1000.0F);
    AddChoice<content::import::SourceUpAxis>(commands, cc::kUpAxis, "Up Axis",
      "Up axis of an FBX source", choices::kUpAxes, [vm](const auto v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.coordinate.up_axis = v; });
      });
    AddChoice<content::import::GeometryAttributePolicy>(commands,
      cc::kNormalPolicy, "Normal Policy", "How normals are imported",
      choices::kGeometryPolicies, [vm](const auto v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.normal_policy = v; });
      });
    AddChoice<content::import::GeometryAttributePolicy>(commands,
      cc::kTangentPolicy, "Tangent Policy", "How tangents are imported",
      choices::kGeometryPolicies, [vm](const auto v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.tangent_policy = v; });
      });
    commands.AddFloat(
      Name(cc::kSmoothingAngle), "Normals Smoothing Angle",
      "Largest angle in degrees between faces sharing a smoothed normal",
      [vm](const float v) -> void {
        EditOptions(
          vm, [v](auto& o) -> void { o.normals_smoothing_angle_deg = v; });
      },
      0.0F, 180.0F);
    commands.AddBool(Name(cc::kGenerateLods), "Generate LODs",
      "Generate LODs for geometries with one LOD", [vm](const bool v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.lod_generation.enabled = v; });
      });
    commands.AddInt(
      Name(cc::kLodLevels), "LOD Levels", "LODs generated after LOD 0",
      [vm](const int v) -> void {
        EditOptions(vm, [v](auto& o) -> void {
          o.lod_generation.levels = static_cast<uint32_t>(v);
        });
      },
      static_cast<int>(LodGenerationOptions::kMinLevels),
      static_cast<int>(LodGenerationOptions::kMaxLevels));
    commands.AddFloat(
      Name(cc::kLodReduction), "LOD Reduction",
      "Triangles each generated LOD removes, in percent of the previous LOD's",
      [vm](const float v) -> void {
        EditOptions(
          vm, [v](auto& o) -> void { o.lod_generation.reduction_percent = v; });
      },
      LodGenerationOptions::kMinReductionPercent,
      LodGenerationOptions::kMaxReductionPercent);
  }

  auto RegisterLightCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddBool(Name(cc::kImportLights), "Import Lights",
      "Import the source's lights", [vm](const bool v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.import_lights = v; });
      });
    commands.AddBool(Name(cc::kImportCameras), "Import Cameras",
      "Import the source's cameras", [vm](const bool v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.import_cameras = v; });
      });
    commands.AddFloat(
      Name(cc::kLightScale), "Light Intensity Scale",
      "Factor every imported light's intensity is multiplied by",
      [vm](const float v) -> void {
        EditOptions(vm, [v](auto& o) -> void { o.light_intensity_scale = v; });
      },
      0.0F, 1000.0F);
  }

  auto RegisterTextureToggles(
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
    toggle(cc::kTuning, "Texture Tuning", "Apply the texture tuning on import",
      &TextureTuning::enabled);
    toggle(cc::kBakeHdr, "Bake HDR To LDR", "Tonemap HDR sources into LDR",
      &TextureTuning::bake_hdr_to_ldr);
    toggle(cc::kFlipGreen, "Flip Normal Green",
      "Flip the normal map's Y channel", &TextureTuning::flip_normal_green);
    toggle(cc::kRenormalize, "Renormalize Mips",
      "Renormalize normal map mip levels",
      &TextureTuning::renormalize_normals_in_mips);
    toggle(cc::kCubemap, "Import As Cubemap", "Import the source as a cubemap",
      &TextureTuning::import_cubemap);
    toggle(cc::kEquirect, "Equirect To Cubemap",
      "Convert an equirectangular source to a cubemap",
      &TextureTuning::equirect_to_cubemap);
    commands.AddFloat(
      Name(cc::kExposureEv), "HDR Bake Exposure",
      "Exposure in EV when baking HDR to LDR",
      [vm](const float v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.exposure_ev = v; });
      },
      -10.0F, 10.0F);
    commands.AddInt(
      Name(cc::kFaceSize), "Cubemap Face Size",
      "Face size in pixels; 0 derives it from the source",
      [vm](const int v) -> void {
        EditTuning(vm, [v](auto& t) -> void {
          t.cubemap_face_size = static_cast<uint32_t>(v);
        });
      },
      0, 8192);
    commands.AddInt(
      Name(cc::kMaxMips), "Max Mips", "Mip levels of the max-count policy",
      [vm](const int v) -> void {
        EditTuning(vm,
          [v](auto& t) -> void { t.max_mip_levels = static_cast<uint8_t>(v); });
      },
      1, 16);
  }

  auto RegisterTextureChoices(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    AddChoice<content::import::TextureIntent>(commands, cc::kIntent,
      "Texture Intent", "What the texture is used for", choices::kIntents,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void {
          t.intent = v;
          t.intent_stated = true;
        });
      });
    AddChoice<ColorSpace>(commands, cc::kColorSpace, "Source Color Space",
      "Color space of the source image", choices::kColorSpaces,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void {
          t.source_color_space = v;
          t.source_color_space_stated = true;
        });
      });
    AddChoice<content::import::MipPolicy>(commands, cc::kMipPolicy,
      "Mip Policy", "Which mip levels are generated", choices::kMipPolicies,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.mip_policy = v; });
      });
    auto sizes = std::vector<std::string> {};
    for (const auto& choice : choices::kMaxSizes) {
      sizes.emplace_back(choice.label);
    }
    commands.AddEnum(Name(cc::kMaxSize), "Max Texture Size",
      "Largest width or height of a stored texture", std::move(sizes),
      [vm](const std::size_t i) -> void {
        EditTuning(
          vm, [size = choices::kMaxSizes.at(i).value](auto& t) -> void {
            t.max_size = size;
          });
      });
    AddChoice<content::import::MipFilter>(commands, cc::kMipFilter,
      "Mip Filter", "Downsampling filter", choices::kMipFilters,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.mip_filter = v; });
      });
    AddChoice<Format>(commands, cc::kColorFormat, "Color Format",
      "Output format of color textures", choices::kFormats,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.color_output_format = v; });
      });
    AddChoice<Format>(commands, cc::kDataFormat, "Data Format",
      "Output format of data textures", choices::kFormats,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.data_output_format = v; });
      });
    AddChoice<content::import::Bc7Quality>(commands, cc::kBc7Quality,
      "BC7 Quality", "BC7 encoder quality", choices::kBc7Qualities,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.bc7_quality = v; });
      });
    AddChoice<content::import::HdrHandling>(commands, cc::kHdrHandling,
      "HDR Handling", "What happens to HDR sources", choices::kHdrHandlings,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.hdr_handling = v; });
      });
    AddChoice<content::import::CubeMapImageLayout>(commands, cc::kCubeLayout,
      "Cube Layout", "Face layout of a cubemap source", choices::kCubeLayouts,
      [vm](const auto v) -> void {
        EditTuning(vm, [v](auto& t) -> void { t.cubemap_layout = v; });
      });
  }

  auto RegisterImporterCommands(
    PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
  {
    commands.AddInt(
      Name(cc::kThreadPool), "Importer Threads",
      "Size of the importer's thread pool; takes effect on restart",
      [vm](const int v) -> void {
        EditProfile(vm, [v](auto& p) -> void {
          p.importer.thread_pool_size = static_cast<uint32_t>(v);
        });
      },
      1, 128);
    for (const auto& pipeline : kImporterPipelines) {
      const auto member = pipeline.member;
      const auto prefix
        = std::string(cc::kPipelinePrefix) + std::string(pipeline.name);
      commands.AddInt(
        prefix + ".workers", std::string(pipeline.label) + " Workers",
        "Workers of the pipeline; takes effect on restart",
        [vm, member](const int v) -> void {
          EditProfile(vm, [member, v](auto& p) -> void {
            (p.importer.concurrency.*member).workers = static_cast<uint32_t>(v);
          });
        },
        1, 64);
      commands.AddInt(
        prefix + ".queue", std::string(pipeline.label) + " Queue",
        "Queue capacity of the pipeline; takes effect on restart",
        [vm, member](const int v) -> void {
          EditProfile(vm, [member, v](auto& p) -> void {
            (p.importer.concurrency.*member).queue_capacity
              = static_cast<uint32_t>(v);
          });
        },
        1, 256);
    }
    commands.AddAction(Name(cc::kRestartImporter), "Restart Importer",
      "Restart the importer with the panel's importer settings",
      [vm] -> void { vm->RestartImportService(); });
  }
} // namespace

auto RegisterContentCommands(
  PanelCommands& commands, observer_ptr<ContentVm> vm) -> void
{
  RegisterMountCommands(commands, vm);
  RegisterSceneCommands(commands, vm);
  RegisterImportTabCommands(commands, vm);
  RegisterContentOptionCommands(commands, vm);
  RegisterGeometryCommands(commands, vm);
  RegisterLightCommands(commands, vm);
  RegisterTextureToggles(commands, vm);
  RegisterTextureChoices(commands, vm);
  RegisterImporterCommands(commands, vm);
}

} // namespace oxygen::examples::ui
