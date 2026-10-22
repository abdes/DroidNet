//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
// Completes the environment that BuildEnvironment returns.
#include <Oxygen/Scene/Environment/SceneEnvironment.h> // IWYU pragma: keep
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/SceneInstantiation/EnvironmentHydration.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>

namespace oxygen::instantiation {

namespace {
  namespace world = data::pak::world;

  //! Instancing deeper than this is treated as a cycle.
  constexpr int kMaxInstanceDepth = 16;

  auto MakeNodeName(const std::string_view name, const size_t index)
    -> std::string
  {
    return name.empty() ? fmt::format("Node{}", index) : std::string(name);
  }

  auto DecodeNodeFlags(const world::NodeRecord& node) -> scene::SceneNode::Flags
  {
    using scene::SceneFlag;
    using scene::SceneNodeFlags;

    scene::SceneNode::Flags flags {};
    const auto apply
      = [&](const SceneNodeFlags flag, const uint32_t mask) -> void {
      const auto inherited = (node.inherited_flags & mask) != 0U;
      const auto local_value = (node.node_flags & mask) != 0U;
      flags = flags.SetFlag(flag,
        SceneFlag {}
          .SetInheritedBit(inherited)
          .SetEffectiveValueBit(local_value)
          .SetPendingValueBit(local_value));
    };
    apply(SceneNodeFlags::kVisible, world::kSceneNodeFlag_Visible);
    apply(SceneNodeFlags::kStatic, world::kSceneNodeFlag_Static);
    apply(SceneNodeFlags::kCastsShadows, world::kSceneNodeFlag_CastsShadows);
    apply(
      SceneNodeFlags::kReceivesShadows, world::kSceneNodeFlag_ReceivesShadows);
    apply(SceneNodeFlags::kRayCastingSelectable,
      world::kSceneNodeFlag_RayCastingSelectable);
    apply(SceneNodeFlags::kIgnoreParentTransform,
      world::kSceneNodeFlag_IgnoreParentTransform);
    return flags;
  }

  auto ApplyCommonLight(scene::CommonLightProperties& dst,
    const world::LightCommonRecord& src) -> void
  {
    dst.affects_world = src.affects_world != 0U;
    dst.color_rgb = { src.color_rgb[0], src.color_rgb[1], src.color_rgb[2] };
    dst.casts_shadows = src.casts_shadows != 0U;
    dst.shadow.bias = src.shadow.bias;
    dst.shadow.normal_bias = src.shadow.normal_bias;
    dst.shadow.contact_shadows = src.shadow.contact_shadows != 0U;
    dst.shadow.resolution_hint
      = static_cast<scene::ShadowResolutionHint>(src.shadow.resolution_hint);
    dst.exposure_compensation_ev = src.exposure_compensation_ev;
  }

  auto MakeDirectionalLight(const world::DirectionalLightRecord& rec)
    -> std::unique_ptr<scene::DirectionalLight>
  {
    auto light = std::make_unique<scene::DirectionalLight>();
    ApplyCommonLight(light->Common(), rec.common);
    light->SetIntensityLux(rec.intensity_lux);
    light->SetAngularSizeRadians(rec.angular_size_radians);
    light->SetAtmosphereLightSlot(
      static_cast<scene::AtmosphereLightSlot>(rec.atmosphere_light_slot));
    light->SetUsePerPixelAtmosphereTransmittance(
      rec.use_per_pixel_atmosphere_transmittance != 0U);
    light->SetAtmosphereDiskLuminanceScale({
      rec.atmosphere_disk_luminance_scale_rgb[0],
      rec.atmosphere_disk_luminance_scale_rgb[1],
      rec.atmosphere_disk_luminance_scale_rgb[2],
    });

    auto csm = light->CascadedShadows();
    csm.cascade_count = std::clamp<std::uint32_t>(
      rec.cascade_count, 1U, scene::kMaxShadowCascades);
    csm.split_mode
      = static_cast<scene::DirectionalCsmSplitMode>(rec.split_mode);
    csm.max_shadow_distance = rec.max_shadow_distance;
    std::ranges::copy(rec.cascade_distances, csm.cascade_distances.begin());
    csm.distribution_exponent = rec.distribution_exponent;
    csm.transition_fraction = rec.transition_fraction;
    csm.distance_fadeout_fraction = rec.distance_fadeout_fraction;
    light->CascadedShadows() = scene::CanonicalizeCascadedShadowSettings(csm);
    return light;
  }

  auto MakePointLight(const world::PointLightRecord& rec)
    -> std::unique_ptr<scene::PointLight>
  {
    auto light = std::make_unique<scene::PointLight>();
    ApplyCommonLight(light->Common(), rec.common);
    light->SetLuminousFluxLm(rec.luminous_flux_lm);
    light->SetRange(std::abs(rec.range));
    light->SetSourceRadius(std::abs(rec.source_radius));
    return light;
  }

  auto MakeSpotLight(const world::SpotLightRecord& rec)
    -> std::unique_ptr<scene::SpotLight>
  {
    auto light = std::make_unique<scene::SpotLight>();
    ApplyCommonLight(light->Common(), rec.common);
    light->SetLuminousFluxLm(rec.luminous_flux_lm);
    light->SetRange(std::abs(rec.range));
    light->SetInnerConeAngleRadians(rec.inner_cone_angle_radians);
    light->SetOuterConeAngleRadians(rec.outer_cone_angle_radians);
    light->SetSourceRadius(std::abs(rec.source_radius));
    return light;
  }

  auto MakePerspectiveCamera(const world::PerspectiveCameraRecord& rec)
    -> std::unique_ptr<scene::PerspectiveCamera>
  {
    auto camera = std::make_unique<scene::PerspectiveCamera>();
    camera->SetFieldOfView(rec.fov_y);
    camera->SetAspectRatio(rec.aspect_ratio);
    camera->SetAspectMode(rec.aspect_mode);
    camera->SetNearPlane(rec.near_plane);
    camera->SetFarPlane(rec.far_plane);
    camera->SetExposure({
      .aperture_f = rec.aperture_f,
      .shutter_rate = rec.shutter_rate,
      .iso = rec.iso,
    });
    return camera;
  }

  auto MakeOrthographicCamera(const world::OrthographicCameraRecord& rec)
    -> std::unique_ptr<scene::OrthographicCamera>
  {
    auto camera = std::make_unique<scene::OrthographicCamera>();
    camera->SetExtents(
      rec.left, rec.right, rec.bottom, rec.top, rec.near_plane, rec.far_plane);
    camera->SetAspectMode(rec.aspect_mode);
    camera->SetExposure({
      .aperture_f = rec.aperture_f,
      .shutter_rate = rec.shutter_rate,
      .iso = rec.iso,
    });
    return camera;
  }

  auto ApplyLocalFogVolume(
    scene::SceneNode& node, const world::LocalFogVolumeRecord& rec) -> bool
  {
    const auto impl = node.GetImpl();
    if (!impl.has_value()) {
      return false;
    }
    auto& node_impl = impl->get();
    if (!node_impl.HasComponent<scene::environment::LocalFogVolume>()) {
      node_impl.AddComponent<scene::environment::LocalFogVolume>();
    }
    auto& fog = node_impl.GetComponent<scene::environment::LocalFogVolume>();
    fog.SetEnabled(rec.enabled != 0U);
    fog.SetRadialFogExtinction(rec.radial_fog_extinction);
    fog.SetHeightFogExtinction(rec.height_fog_extinction);
    fog.SetHeightFogFalloff(rec.height_fog_falloff);
    fog.SetHeightFogOffset(rec.height_fog_offset);
    fog.SetFogPhaseG(rec.fog_phase_g);
    fog.SetFogAlbedo(
      { rec.fog_albedo[0], rec.fog_albedo[1], rec.fog_albedo[2] });
    fog.SetFogEmissive(
      { rec.fog_emissive[0], rec.fog_emissive[1], rec.fog_emissive[2] });
    fog.SetSortPriority(rec.sort_priority);
    return true;
  }

  //! Awaits a loader callback; the callback may run before the await.
  template <typename Asset> struct PendingLoad {
    co::Event done;
    std::shared_ptr<Asset> asset;
  };

  //! Builds the nodes of one scene and of the scenes it instances.
  class Instantiator final {
  public:
    Instantiator(content::IAssetLoader& loader, scene::Scene& target,
      const observer_ptr<const scripting::SceneScriptAttachment> scripts,
      SceneInstance& result)
      : loader_(&loader)
      , target_(&target)
      , scripts_(scripts)
      , result_(&result)
    {
    }

    //! Creates `asset`'s nodes, under `parent` when it is instanced.
    // The caller awaits at once and owns the referenced arguments.
    auto Build(const data::SceneAsset& asset,
      std::optional<scene::SceneNode> parent, const int depth)
      -> co::Co<std::vector<scene::SceneNode>>
    {
      auto nodes = CreateNodes(asset, parent);
      AttachRenderables(asset, nodes);
      AttachCameras(asset, nodes);
      AttachLights(asset, nodes);
      AttachLocalFogVolumes(asset, nodes);
      co_await InstantiateInstances(asset, nodes, depth);
      co_return nodes;
    }

    //! Attaches the scripts of a built scene.
    auto AttachScripts(const data::SceneAsset& asset,
      const std::vector<scene::SceneNode>& nodes) -> void
    {
      if (scripts_ == nullptr) {
        return;
      }
      auto attached = scripts_->Attach(asset, nodes);
      std::ranges::move(attached.slots, std::back_inserter(result_->scripts));
    }

    auto Issue(const data::SceneAsset& asset,
      const std::optional<uint32_t> node_index, std::string message) -> void
    {
      LOG_F(WARNING, "scene {}{}: {}", data::to_string(asset.GetAssetKey()),
        node_index.has_value() ? fmt::format(" node {}", *node_index)
                               : std::string {},
        message);
      result_->issues.push_back({
        .scene = asset.GetAssetKey(),
        .node_index = node_index,
        .message = std::move(message),
      });
    }

    auto LoadScene(const data::AssetKey key)
      -> co::Co<std::shared_ptr<data::SceneAsset>>
    {
      auto pending = std::make_shared<PendingLoad<data::SceneAsset>>();
      loader_->StartLoadScene(
        key, [pending](std::shared_ptr<data::SceneAsset> asset) -> void {
          pending->asset = std::move(asset);
          pending->done.Trigger();
        });
      co_await pending->done;
      co_return std::move(pending->asset);
    }

    auto LoadInputContext(const data::AssetKey key)
      -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>
    {
      auto pending
        = std::make_shared<PendingLoad<data::InputMappingContextAsset>>();
      loader_->StartLoadInputMappingContextAsset(key,
        [pending](
          std::shared_ptr<data::InputMappingContextAsset> asset) -> void {
          pending->asset = std::move(asset);
          pending->done.Trigger();
        });
      co_await pending->done;
      co_return std::move(pending->asset);
    }

    [[nodiscard]] auto Loader() const -> content::IAssetLoader&
    {
      return *loader_;
    }

  private:
    auto CreateNodes(const data::SceneAsset& asset,
      std::optional<scene::SceneNode> parent) -> std::vector<scene::SceneNode>
    {
      const auto records = asset.GetNodes();
      auto nodes = std::vector<scene::SceneNode> {};
      nodes.reserve(records.size());
      for (const auto [index, record] : std::views::enumerate(records)) {
        auto node = target_->CreateNode(
          MakeNodeName(asset.GetNodeName(record), static_cast<size_t>(index)),
          DecodeNodeFlags(record));
        auto transform = node.GetTransform();
        transform.SetLocalPosition({ record.translation[0],
          record.translation[1], record.translation[2] });
        transform.SetLocalRotation(Quat(record.rotation[3], record.rotation[0],
          record.rotation[1], record.rotation[2]));
        transform.SetLocalScale(
          { record.scale[0], record.scale[1], record.scale[2] });
        nodes.push_back(std::move(node));
      }

      for (const auto [index, record] : std::views::enumerate(records)) {
        const auto node_index = static_cast<uint32_t>(index);
        auto& node = nodes.at(static_cast<size_t>(index));
        const auto parent_index = record.parent_index;
        if (parent_index == node_index) {
          if (parent.has_value()
            && !target_->ReparentNode(node, *parent, false)) {
            Issue(
              asset, node_index, "cannot place the node under its instance");
          }
          continue;
        }
        if (!target_->ReparentNode(node, nodes.at(parent_index), false)) {
          Issue(asset, node_index,
            fmt::format("cannot parent the node to node {}", parent_index));
        }
      }
      return nodes;
    }

    auto AttachRenderables(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes) -> void
    {
      auto overrides = std::unordered_multimap<world::SceneNodeIndexT,
        const world::MaterialOverrideRecord*> {};
      for (const auto& record :
        asset.GetComponents<world::MaterialOverrideRecord>()) {
        overrides.emplace(record.node_index, &record);
      }

      for (const auto& record :
        asset.GetComponents<world::RenderableRecord>()) {
        if (record.visible == 0U) {
          continue;
        }
        auto geometry = loader_->GetGeometryAsset(record.geometry_key, asset);
        if (!geometry) {
          Issue(asset, record.node_index,
            fmt::format("geometry {} is not loaded",
              data::to_string(record.geometry_key)));
          continue;
        }
        auto renderable = nodes.at(record.node_index).GetRenderable();
        renderable.SetGeometry(geometry);
        const auto [first, last] = overrides.equal_range(record.node_index);
        for (const auto& [node_index, assignment] :
          std::ranges::subrange(first, last)) {
          auto material
            = loader_->GetMaterialAsset(assignment->material_key, asset);
          const auto current = assignment->layout_revision
            == geometry->Surfaces().layout_revision;
          if (!material || !current
            || !renderable.SetMaterialOverride(
              assignment->surface_id, std::move(material))) {
            Issue(asset, node_index,
              "a material override is not loaded or no longer matches the "
              "geometry's surfaces");
          }
        }
      }
    }

    auto AttachCameras(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes) -> void
    {
      for (const auto& record :
        asset.GetComponents<world::PerspectiveCameraRecord>()) {
        if (!nodes.at(record.node_index)
              .AttachCamera(MakePerspectiveCamera(record))) {
          Issue(asset, record.node_index, "cannot attach the camera");
        }
      }
      for (const auto& record :
        asset.GetComponents<world::OrthographicCameraRecord>()) {
        if (!nodes.at(record.node_index)
              .AttachCamera(MakeOrthographicCamera(record))) {
          Issue(asset, record.node_index, "cannot attach the camera");
        }
      }
    }

    auto AttachLights(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes) -> void
    {
      const auto attach
        = [&](const uint32_t node_index, std::unique_ptr<Component> light,
            const std::string_view kind) -> void {
        if (!nodes.at(node_index).ReplaceLight(std::move(light))) {
          Issue(asset, node_index, fmt::format("cannot attach the {}", kind));
        }
      };
      for (const auto& record :
        asset.GetComponents<world::DirectionalLightRecord>()) {
        attach(
          record.node_index, MakeDirectionalLight(record), "directional light");
      }
      for (const auto& record :
        asset.GetComponents<world::PointLightRecord>()) {
        attach(record.node_index, MakePointLight(record), "point light");
      }
      for (const auto& record : asset.GetComponents<world::SpotLightRecord>()) {
        attach(record.node_index, MakeSpotLight(record), "spot light");
      }
    }

    auto AttachLocalFogVolumes(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes) -> void
    {
      for (const auto& record :
        asset.GetComponents<world::LocalFogVolumeRecord>()) {
        if (!ApplyLocalFogVolume(nodes.at(record.node_index), record)) {
          Issue(asset, record.node_index, "cannot attach the local fog volume");
        }
      }
    }

    // The caller awaits at once and owns the referenced arguments.
    auto InstantiateInstances(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes, const int depth) -> co::Co<>
    {
      for (const auto& record :
        asset.GetComponents<world::SceneInstanceRecord>()) {
        const auto key_text = data::to_string(record.scene_key);
        if (depth >= kMaxInstanceDepth) {
          Issue(asset, record.node_index,
            fmt::format(
              "instancing scene {} nests too deeply (a cycle?)", key_text));
          continue;
        }
        const auto instanced = co_await LoadScene(record.scene_key);
        if (!instanced) {
          Issue(asset, record.node_index,
            fmt::format("instanced scene {} is not available", key_text));
          continue;
        }
        const auto instance_nodes
          = co_await Build(*instanced, nodes.at(record.node_index), depth + 1);
        AttachScripts(*instanced, instance_nodes);
      }
    }

    observer_ptr<content::IAssetLoader> loader_;
    observer_ptr<scene::Scene> target_;
    observer_ptr<const scripting::SceneScriptAttachment> scripts_;
    observer_ptr<SceneInstance> result_;
  };

  auto ApplyEnvironment(Instantiator& instantiator,
    const data::SceneAsset& asset, scene::Scene& target) -> void
  {
    try {
      target.SetEnvironment(BuildEnvironment(asset, instantiator.Loader()));
    } catch (const std::exception& error) {
      instantiator.Issue(asset, std::nullopt,
        fmt::format("the environment is not applied: {}", error.what()));
    }
  }

  auto ApplyStartingCamera(Instantiator& instantiator,
    const data::SceneAsset& asset, scene::Scene& target,
    std::vector<scene::SceneNode>& nodes) -> void
  {
    if (const auto start = asset.GetStartingCamera(); start.has_value()) {
      if (!target.SetActiveCamera(nodes.at(*start))) {
        instantiator.Issue(
          asset, *start, "the starting camera cannot be made active");
      }
      return;
    }
    const auto camera = std::ranges::find_if(
      nodes, [](scene::SceneNode& node) -> bool { return node.HasCamera(); });
    if (camera == nodes.end()) {
      instantiator.Issue(asset, std::nullopt, "the scene has no camera");
      return;
    }
    const auto index = static_cast<uint32_t>(camera - nodes.begin());
    (void)target.SetActiveCamera(*camera);
    instantiator.Issue(asset, index,
      fmt::format("no starting camera is authored; viewing through '{}', the "
                  "first camera",
        camera->GetName()));
  }

  // The caller awaits at once and owns the referenced arguments.
  auto StartInputContexts(Instantiator& instantiator,
    const data::SceneAsset& asset, engine::InputSystem& input) -> co::Co<>
  {
    for (const auto& record :
      asset.GetComponents<world::StartingInputContextRecord>()) {
      const auto key_text = data::to_string(record.context_key);
      if (auto added = input.GetMappingContextByKey(record.context_key)) {
        input.ActivateMappingContext(added);
        continue;
      }
      const auto context_asset
        = co_await instantiator.LoadInputContext(record.context_key);
      if (!context_asset) {
        instantiator.Issue(asset, std::nullopt,
          fmt::format("starting input context {} is not available", key_text));
        continue;
      }
      auto context = content::HydrateInputContext(
        *context_asset, instantiator.Loader(), input);
      if (!context) {
        instantiator.Issue(asset, std::nullopt,
          fmt::format(
            "starting input context {} cannot be hydrated", key_text));
        continue;
      }
      input.AddMappingContext(context, record.priority);
      input.ActivateMappingContext(context);
    }
  }

} // namespace

// The caller awaits at once and owns the referenced arguments.
auto Instantiate(SceneInstantiationRequest request,
  content::IAssetLoader& loader) -> co::Co<SceneInstance>
{
  auto result = SceneInstance {};
  if (!request.asset || request.target == nullptr) {
    co_return result;
  }
  const auto& asset = *request.asset;
  auto& target = *request.target;
  auto instantiator = Instantiator(loader, target, request.scripts, result);

  ApplyEnvironment(instantiator, asset, target);
  result.nodes = co_await instantiator.Build(asset, std::nullopt, 0);
  try {
    target.GetDirectionalLightResolver().Validate();
  } catch (const std::exception& error) {
    instantiator.Issue(asset, std::nullopt, error.what());
  }
  ApplyStartingCamera(instantiator, asset, target, result.nodes);
  if (request.input != nullptr) {
    co_await StartInputContexts(instantiator, asset, *request.input);
  }
  instantiator.AttachScripts(asset, result.nodes);
  co_return result;
}

} // namespace oxygen::instantiation
