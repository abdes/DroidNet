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
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
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
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/SceneInstantiation/EnvironmentHydration.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>

namespace oxygen::instantiation {

namespace {
  namespace world = data::pak::world;

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

  //! Writes a node's flags and local transform; applying it again replaces
  //! them.
  auto ApplyNode(scene::SceneNode& node, const world::NodeRecord& record)
    -> void
  {
    if (auto flags = node.GetFlags(); flags.has_value()) {
      flags->get() = DecodeNodeFlags(record);
    }
    auto transform = node.GetTransform();
    transform.SetLocalPosition(
      { record.translation[0], record.translation[1], record.translation[2] });
    transform.SetLocalRotation(Quat(record.rotation[3], record.rotation[0],
      record.rotation[1], record.rotation[2]));
    transform.SetLocalScale(
      { record.scale[0], record.scale[1], record.scale[2] });
  }

  //! Attaches or replaces the node's camera.
  auto ApplyCamera(scene::SceneNode& node,
    const world::PerspectiveCameraRecord& record) -> bool
  {
    return node.ReplaceCamera(MakePerspectiveCamera(record));
  }

  auto ApplyCamera(scene::SceneNode& node,
    const world::OrthographicCameraRecord& record) -> bool
  {
    return node.ReplaceCamera(MakeOrthographicCamera(record));
  }

  //! Attaches or replaces the node's light.
  auto ApplyLight(
    scene::SceneNode& node, const world::DirectionalLightRecord& record) -> bool
  {
    return node.ReplaceLight(MakeDirectionalLight(record));
  }

  auto ApplyLight(scene::SceneNode& node, const world::PointLightRecord& record)
    -> bool
  {
    return node.ReplaceLight(MakePointLight(record));
  }

  auto ApplyLight(scene::SceneNode& node, const world::SpotLightRecord& record)
    -> bool
  {
    return node.ReplaceLight(MakeSpotLight(record));
  }

  //! Builds the composed tree of one scene and the scenes it instances.
  class Instantiator final {
  public:
    //! `target` may be null when no node is created, as for a live change.
    Instantiator(content::IAssetLoader& loader,
      const observer_ptr<scene::Scene> target,
      const observer_ptr<const scripting::SceneScriptAttachment> scripts,
      InstantiationResult& result)
      : loader_(&loader)
      , target_(target)
      , scripts_(scripts)
      , result_(&result)
    {
    }

    //! Loads `asset` and every scene reachable through its instances, then
    //! composes it.
    // The caller awaits at once and owns the referenced arguments.
    auto Compose(std::shared_ptr<const data::SceneAsset> asset) -> co::Co<>
    {
      const auto loaded = co_await LoadInstanced(asset);
      composed_ = data::ComposeScene(std::move(asset), MakeResolver(loaded));
      ReportComposition();
    }

    //! Loads `source` and the scenes it instances, then composes it as an
    //! instance under `root` with a host's `changes`.
    // The caller awaits at once and owns the referenced arguments.
    auto ComposeUnder(scene::SceneNode root, const data::AssetKey source,
      std::vector<data::SceneChange> changes,
      std::vector<scene::SceneNode> host_nodes,
      std::vector<data::AssetKey> ancestors) -> co::Co<>
    {
      root_ = std::move(root);
      root_changes_ = std::move(changes);
      host_nodes_ = std::move(host_nodes);
      auto loaded = LoadedScenes {};
      // A cycle loads nothing: composing reports it.
      if (!std::ranges::contains(ancestors, source)) {
        if (auto asset = co_await LoadScene(source)) {
          loaded = co_await LoadInstanced(std::move(asset));
        }
      }
      composed_ = data::ComposeInstance(
        data::InstanceComposition {
          .source = source,
          .changes = root_changes_,
          .ancestors = ancestors,
        },
        MakeResolver(loaded));
      ReportComposition();
    }

    //! Applies the records with `composed`, made from live instances.
    auto UseComposition(data::ComposedScene composed) -> void
    {
      composed_ = std::move(composed);
    }

    //! Builds the nodes of every occurrence, parents first, and makes each
    //! instance root an instance.
    auto Build() -> void
    {
      const auto& occurrences = composed_.occurrences;
      nodes_.resize(occurrences.size());
      for (std::size_t index = 0; index < occurrences.size(); ++index) {
        BuildOccurrence(index);
      }
      for (std::size_t index = 0; index < occurrences.size(); ++index) {
        AttachInstance(index);
      }
    }

    //! The nodes created for the first occurrence: the requested scene, or
    //! the instanced one.
    [[nodiscard]] auto RootNodes() const -> std::vector<scene::SceneNode>
    {
      return nodes_.empty() ? std::vector<scene::SceneNode> {} : nodes_.front();
    }

    //! Applies again the records of a node that `address` sets, after a
    //! change of it: the node's flags and transform, the component the
    //! property belongs to, or the renderable's overrides.
    auto Reapply(const std::size_t occurrence, scene::SceneNode& node,
      const data::ComposedNode& composed, const data::PropertyAddress& address)
      -> void
    {
      using data::schema::ComponentId;
      const auto* property
        = data::schema::FindOverrideProperty(address.override_id);
      if (property == nullptr) {
        return;
      }
      if (property->asset_sourced) {
        ClearOverride(node, address);
        ApplyRenderable(SceneOf(occurrence), node, composed);
        return;
      }
      if (property->component == ComponentId::kTransform
        || property->component == ComponentId::kNode) {
        ApplyNode(node, composed.node);
        return;
      }
      ApplyComponents(occurrence, node, composed, property->component);
    }

    //! The node created for node `index` of an occurrence.
    [[nodiscard]] auto NodeOf(const std::size_t occurrence,
      const uint32_t index) const -> scene::SceneNode
    {
      return nodes_.at(occurrence).at(index);
    }

    [[nodiscard]] auto Composed() const -> const data::ComposedScene&
    {
      return composed_;
    }

    //! Attaches the scripts of every occurrence, the requested scene's last.
    auto AttachScripts() -> void
    {
      if (scripts_ == nullptr) {
        return;
      }
      const auto attach = [this](const std::size_t occurrence) -> void {
        auto attached
          = scripts_->Attach(composed_, occurrence, nodes_, host_nodes_);
        std::ranges::move(attached.slots, std::back_inserter(result_->scripts));
      };
      for (std::size_t index = 1; index < nodes_.size(); ++index) {
        attach(index);
      }
      attach(0U);
    }

    auto Issue(const data::SceneAsset& asset, const std::string_view code,
      const std::optional<uint32_t> node_index, std::string message,
      std::vector<data::NodeId> node_path = {}) -> void
    {
      Issue(asset.GetAssetKey(), code, node_index, std::move(message),
        std::move(node_path));
    }

    //! An issue of the scene `scene` names; nil for a host's change.
    auto Issue(const data::AssetKey& scene, const std::string_view code,
      const std::optional<uint32_t> node_index, std::string message,
      std::vector<data::NodeId> node_path = {}) -> void
    {
      LOG_F(WARNING, "scene {}{}: {} ({})", data::to_string(scene),
        node_index.has_value() ? fmt::format(" node {}", *node_index)
                               : std::string {},
        message, code);
      result_->issues.push_back({
        .code = std::string(code),
        .scene = scene,
        .node_index = node_index,
        .node_path = std::move(node_path),
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
    using LoadedScenes = std::unordered_map<data::AssetKey,
      std::shared_ptr<const data::SceneAsset>>;

    [[nodiscard]] static auto MakeResolver(const LoadedScenes& loaded)
      -> data::SceneResolver
    {
      return [&loaded](const data::AssetKey& key)
               -> std::shared_ptr<const data::SceneAsset> {
        const auto found = loaded.find(key);
        return found != loaded.end() ? found->second : nullptr;
      };
    }

    //! Loads `asset`'s instanced scenes, recursively; `asset` included.
    // The caller awaits at once and owns the referenced arguments.
    auto LoadInstanced(std::shared_ptr<const data::SceneAsset> asset)
      -> co::Co<LoadedScenes>
    {
      auto loaded = LoadedScenes {};
      auto pending = std::vector<std::shared_ptr<const data::SceneAsset>> {
        asset,
      };
      loaded.emplace(asset->GetAssetKey(), asset);
      while (!pending.empty()) {
        const auto scene = std::move(pending.back());
        pending.pop_back();
        for (const auto& record :
          scene->GetComponents<world::SceneInstanceRecord>()) {
          if (loaded.contains(record.scene_key)) {
            continue;
          }
          auto instanced = co_await LoadScene(record.scene_key);
          loaded.emplace(record.scene_key, instanced);
          if (instanced) {
            pending.push_back(std::move(instanced));
          }
        }
      }
      co_return loaded;
    }

    [[nodiscard]] auto SceneOf(const std::size_t occurrence) const
      -> const data::SceneAsset&
    {
      return *composed_.occurrences.at(occurrence).scene;
    }

    //! The scene that supplied a value: an occurrence's scene; null for a
    //! host, which supplies no scene (`data::kRequester`).
    [[nodiscard]] auto SupplierOf(const data::ChangeSupplier supplier) const
      -> const data::SceneAsset*
    {
      return supplier < composed_.occurrences.size()
        ? composed_.occurrences.at(supplier).scene.get()
        : nullptr;
    }

    auto ReportComposition() -> void
    {
      for (const auto& issue : composed_.issues) {
        auto message = issue.scene.IsNil()
          ? std::string { "a record of the scene is invalid" }
          : fmt::format("instanced scene {}", data::to_string(issue.scene));
        // An instance that cannot be composed at all has no occurrence.
        if (issue.occurrence >= composed_.occurrences.size()) {
          Issue(issue.scene, data::to_string(issue.code), std::nullopt,
            std::move(message));
          continue;
        }
        Issue(SceneOf(issue.occurrence), data::to_string(issue.code),
          issue.node_index, std::move(message));
      }
      for (const auto& unresolved : composed_.unresolved) {
        const auto* supplier = SupplierOf(unresolved.supplier);
        Issue(supplier != nullptr ? supplier->GetAssetKey() : data::AssetKey {},
          issue_codes::kChangeUnresolved,
          supplier != nullptr ? std::optional { unresolved.instance_root }
                              : std::nullopt,
          "the change names no node of the instanced scene, or a property "
          "that node does not have",
          unresolved.change.path);
      }
    }

    auto BuildOccurrence(const std::size_t index) -> void
    {
      const auto& occurrence = composed_.occurrences.at(index);
      const auto& asset = *occurrence.scene;
      auto parent = root_;
      if (occurrence.parent.has_value()) {
        parent = nodes_.at(*occurrence.parent).at(occurrence.instance_root);
      }
      auto& nodes = nodes_.at(index);
      nodes.reserve(occurrence.nodes.size());
      for (const auto& composed : occurrence.nodes) {
        auto node = target_->CreateNode(
          MakeNodeName(
            asset.GetNodeName(asset.GetNode(composed.index)), composed.index),
          DecodeNodeFlags(composed.node));
        ApplyNode(node, composed.node);
        nodes.push_back(std::move(node));
      }
      for (const auto& composed : occurrence.nodes) {
        Parent(asset, nodes, composed, parent);
        ApplyComponents(index, nodes.at(composed.index), composed);
      }
    }

    auto Parent(const data::SceneAsset& asset,
      std::vector<scene::SceneNode>& nodes, const data::ComposedNode& composed,
      std::optional<scene::SceneNode>& parent) -> void
    {
      auto& node = nodes.at(composed.index);
      const auto parent_index = composed.node.parent_index;
      if (parent_index == composed.index) {
        if (parent.has_value()
          && !target_->ReparentNode(node, *parent, false)) {
          Issue(asset, issue_codes::kNodeParentInvalid, composed.index,
            "cannot place the node under its instance");
        }
        return;
      }
      if (!target_->ReparentNode(node, nodes.at(parent_index), false)) {
        Issue(asset, issue_codes::kNodeParentInvalid, composed.index,
          fmt::format("cannot parent the node to node {}", parent_index));
      }
    }

    //! Applies the composed records of one node, or only those of the
    //! component `only`; applying them again replaces what they set.
    auto ApplyComponents(const std::size_t occurrence, scene::SceneNode& node,
      const data::ComposedNode& composed,
      const std::optional<data::schema::ComponentId> only = std::nullopt)
      -> void
    {
      using data::schema::ComponentId;
      const auto& asset = SceneOf(occurrence);
      const auto check
        = [&](const bool applied, const std::string_view kind) -> void {
        if (!applied) {
          Issue(asset, issue_codes::kComponentNotAttached, composed.index,
            fmt::format("cannot attach the {}", kind));
        }
      };
      const auto wanted = [only](const ComponentId component) -> bool {
        return !only.has_value() || *only == component;
      };
      if (!only.has_value() && composed.renderable.has_value()) {
        ApplyRenderable(asset, node, composed);
      }
      if (wanted(ComponentId::kPerspectiveCamera)
        && composed.perspective_camera.has_value()) {
        check(ApplyCamera(node, *composed.perspective_camera), "camera");
      }
      if (wanted(ComponentId::kOrthographicCamera)
        && composed.orthographic_camera.has_value()) {
        check(ApplyCamera(node, *composed.orthographic_camera), "camera");
      }
      if (wanted(ComponentId::kDirectionalLight)
        && composed.directional_light.has_value()) {
        check(ApplyLight(node, *composed.directional_light), "light");
      }
      if (wanted(ComponentId::kPointLight)
        && composed.point_light.has_value()) {
        check(ApplyLight(node, *composed.point_light), "light");
      }
      if (wanted(ComponentId::kSpotLight) && composed.spot_light.has_value()) {
        check(ApplyLight(node, *composed.spot_light), "light");
      }
      if (wanted(ComponentId::kLocalFogVolume)
        && composed.local_fog_volume.has_value()) {
        check(ApplyLocalFogVolume(node, *composed.local_fog_volume),
          "local fog volume");
      }
    }

    auto ApplyRenderable(const data::SceneAsset& asset, scene::SceneNode& node,
      const data::ComposedNode& composed) -> void
    {
      if (!composed.renderable.has_value()) {
        return;
      }
      const auto& record = *composed.renderable;
      auto geometry = loader_->GetGeometryAsset(record.geometry_key, asset);
      if (!geometry) {
        Issue(asset, issue_codes::kGeometryMissing, composed.index,
          fmt::format(
            "geometry {} is not loaded", data::to_string(record.geometry_key)));
        return;
      }
      auto renderable = node.GetRenderable();
      renderable.SetGeometry(geometry);
      for (const auto& entry : composed.overrides) {
        ApplyOverride(asset, *geometry, renderable, composed.index, entry);
      }
    }

    //! Applies one override; its value is decoded by the value type of the
    //! property its id names, with the tables of the scene that supplied it.
    auto ApplyOverride(const data::SceneAsset& asset,
      const data::GeometryAsset& geometry,
      scene::SceneNode::Renderable& renderable, const uint32_t node_index,
      const data::ComposedOverride& entry) -> void
    {
      using data::schema::OverrideId;
      const auto& record = entry.record;
      const auto* supplier = SupplierOf(entry.supplier);
      const auto* property
        = data::schema::FindOverrideProperty(record.override_id);
      DCHECK_NOTNULL_F(property);
      // A host's change names a surface of the geometry as loaded; only a
      // scene's can be stale.
      if (supplier != nullptr
        && property->part_kind == data::schema::PartKind::kSurface
        && record.layout_revision != geometry.Surfaces().layout_revision) {
        Issue(asset, issue_codes::kOverrideNotApplied, node_index,
          fmt::format("the override of {} no longer matches the geometry's "
                      "surfaces",
            property->key));
        return;
      }
      auto applied = true;
      switch (record.override_id) {
      case OverrideId::kGeometrySurfaceMaterial: {
        DCHECK_F(property->type == data::schema::ValueType::kAsset);
        auto material = supplier != nullptr
          ? loader_->GetMaterialAsset(record.value.as_asset, *supplier)
          : loader_->GetMaterialAsset(record.value.as_asset);
        applied = material
          && renderable.SetMaterialOverride(
            record.part_id, std::move(material));
        break;
      }
      case OverrideId::kGeometrySurfaceVisibility:
        DCHECK_F(property->type == data::schema::ValueType::kBool);
        applied = renderable.SetVisibilityOverride(
          record.part_id, record.value.as_bool != 0U);
        break;
      case OverrideId::kGeometrySurfaceCastsShadows:
        DCHECK_F(property->type == data::schema::ValueType::kBool);
        applied = renderable.SetCastsShadowsOverride(
          record.part_id, record.value.as_bool != 0U);
        break;
      case OverrideId::kGeometryLodPolicy: {
        DCHECK_F(property->type == data::schema::ValueType::kLodPolicy);
        // A host's change cannot hold a LOD policy (EncodeChange).
        if (supplier == nullptr) {
          applied = false;
          break;
        }
        const auto policies
          = supplier->GetComponents<data::pak::geometry::LodPolicyRecord>();
        renderable.SetLodPolicyOverride(
          scene::MakeLodPolicy(policies[record.value.as_record_index.get()]));
        break;
      }
      default:
        // Composition keeps only the geometry's values as overrides.
        ABORT_F("A composed override is an asset-sourced property");
      }
      if (!applied) {
        Issue(asset, issue_codes::kOverrideNotApplied, node_index,
          fmt::format("the override of {} is not loaded or names a surface "
                      "the geometry does not declare",
            property->key));
      }
    }

    //! Removes the renderable's value of the property `address` sets, so a
    //! cleared change leaves none.
    static auto ClearOverride(
      scene::SceneNode& node, const data::PropertyAddress& address) -> void
    {
      using data::schema::OverrideId;
      auto renderable = node.GetRenderable();
      if (!renderable.HasGeometry()) {
        return;
      }
      switch (address.override_id) {
      case OverrideId::kGeometrySurfaceMaterial:
        (void)renderable.ClearMaterialOverride(address.part_id);
        break;
      case OverrideId::kGeometrySurfaceVisibility:
        (void)renderable.ClearVisibilityOverride(address.part_id);
        break;
      case OverrideId::kGeometrySurfaceCastsShadows:
        (void)renderable.ClearCastsShadowsOverride(address.part_id);
        break;
      case OverrideId::kGeometryLodPolicy:
        renderable.ClearLodPolicyOverride();
        break;
      default:
        break;
      }
    }

    //! Gives the instance root of `occurrence` its `SceneInstanceComponent`:
    //! a node of its parent occurrence, or the host's root.
    auto AttachInstance(const std::size_t index) -> void
    {
      const auto& occurrence = composed_.occurrences.at(index);
      if (!occurrence.parent.has_value()) {
        if (root_.has_value()) {
          MakeInstance(*root_, index, std::nullopt, root_changes_);
        }
        return;
      }
      const auto parent = *occurrence.parent;
      auto changes = std::vector<data::SceneChange> {};
      for (const auto& reaching : occurrence.changes) {
        if (reaching.supplier == parent && reaching.consumed == 0U) {
          changes.push_back(reaching.change);
        }
      }
      MakeInstance(nodes_.at(parent).at(occurrence.instance_root), index,
        parent, std::move(changes));
    }

    //! `requester` is the parent occurrence; none for the host's root.
    auto MakeInstance(scene::SceneNode& root, const std::size_t index,
      const std::optional<std::size_t> requester,
      std::vector<data::SceneChange> changes) -> void
    {
      const auto& occurrence = composed_.occurrences.at(index);
      auto created = std::unordered_map<data::NodeId, scene::NodeHandle> {};
      for (const auto& [composed, node] :
        std::views::zip(occurrence.nodes, nodes_.at(index))) {
        created.emplace(composed.node.node_id, node.GetHandle());
      }
      const auto impl = root.GetImpl();
      if (!impl.has_value()) {
        return;
      }
      auto& root_impl = impl->get();
      if (root_impl.HasComponent<scene::SceneInstanceComponent>()) {
        root_impl.RemoveComponent<scene::SceneInstanceComponent>();
      }
      if (!requester.has_value()) {
        root_impl.AddComponent<scene::SceneInstanceComponent>(occurrence.scene,
          nullptr, nullptr, std::move(changes), std::move(created),
          host_nodes_);
        return;
      }
      root_impl.AddComponent<scene::SceneInstanceComponent>(occurrence.scene,
        composed_.occurrences.at(*requester).scene, SharedNodes(*requester),
        std::move(changes), std::move(created));
    }

    //! The nodes created for `occurrence`, one table shared by the instance
    //! roots it requests.
    auto SharedNodes(const std::size_t occurrence)
      -> std::shared_ptr<const std::vector<scene::SceneNode>>
    {
      shared_nodes_.resize(nodes_.size());
      auto& shared = shared_nodes_.at(occurrence);
      if (!shared) {
        shared = std::make_shared<const std::vector<scene::SceneNode>>(
          nodes_.at(occurrence));
      }
      return shared;
    }

    observer_ptr<content::IAssetLoader> loader_;
    observer_ptr<scene::Scene> target_;
    observer_ptr<const scripting::SceneScriptAttachment> scripts_;
    observer_ptr<InstantiationResult> result_;
    data::ComposedScene composed_;
    //! The nodes created for each occurrence, by node index.
    std::vector<std::vector<scene::SceneNode>> nodes_;
    //! The host's node the first occurrence is instanced under, if any, and
    //! the host's changes.
    std::optional<scene::SceneNode> root_;
    std::vector<data::SceneChange> root_changes_;
    //! The nodes the host's node values name, by position.
    std::vector<scene::SceneNode> host_nodes_;
    //! The created nodes of an occurrence, as its instance roots share them.
    std::vector<std::shared_ptr<const std::vector<scene::SceneNode>>>
      shared_nodes_;
  };

  auto ApplyEnvironment(Instantiator& instantiator,
    const data::SceneAsset& asset, scene::Scene& target) -> void
  {
    try {
      target.SetEnvironment(BuildEnvironment(asset, instantiator.Loader()));
    } catch (const std::exception& error) {
      instantiator.Issue(asset, issue_codes::kEnvironmentInvalid, std::nullopt,
        fmt::format("the environment is not applied: {}", error.what()));
    }
  }

  //! The node the starting camera names, an owned node or one an instance
  //! brings in, when it has a camera.
  auto FindStartingCamera(
    const data::SceneAsset& asset, const std::vector<scene::SceneNode>& nodes)
    -> std::optional<scene::SceneNode>
  {
    const auto start = asset.GetStartingCamera();
    if (!start.has_value()) {
      return std::nullopt;
    }
    auto node = std::optional { nodes.at(start->node_index) };
    if (start->node_path != world::kNoNodePath) {
      node = node->GetInstance().Find(asset.GetNodePath(*start));
    }
    if (!node.has_value() || !node->HasCamera()) {
      return std::nullopt;
    }
    return node;
  }

  auto ApplyStartingCamera(Instantiator& instantiator,
    const data::SceneAsset& asset, scene::Scene& target) -> void
  {
    const auto start = asset.GetStartingCamera();
    if (auto camera = FindStartingCamera(asset, instantiator.RootNodes());
      camera.has_value()) {
      if (!target.SetActiveCamera(*camera)) {
        instantiator.Issue(asset, issue_codes::kStartingCameraUnresolved,
          start.has_value() ? std::optional { start->node_index }
                            : std::nullopt,
          "the starting camera cannot be made active");
      }
      return;
    }
    if (start.has_value()) {
      instantiator.Issue(asset, issue_codes::kStartingCameraUnresolved,
        start->node_index, "the starting camera names no node with a camera",
        asset.GetNodePath(*start));
    }
    // The first camera in composed order: occurrences depth first, nodes in
    // index order.
    const auto& occurrences = instantiator.Composed().occurrences;
    for (std::size_t index = 0; index < occurrences.size(); ++index) {
      for (const auto& composed : occurrences.at(index).nodes) {
        if (!composed.perspective_camera.has_value()
          && !composed.orthographic_camera.has_value()) {
          continue;
        }
        auto camera = instantiator.NodeOf(index, composed.index);
        if (!camera.HasCamera()) {
          continue;
        }
        (void)target.SetActiveCamera(camera);
        if (!start.has_value()) {
          instantiator.Issue(asset, issue_codes::kStartingCameraMissing,
            std::nullopt,
            fmt::format("no starting camera is authored; viewing through "
                        "'{}', the first camera",
              camera.GetName()));
        }
        return;
      }
    }
    instantiator.Issue(
      asset, issue_codes::kNoCamera, std::nullopt, "the scene has no camera");
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
        instantiator.Issue(asset, issue_codes::kInputContextInvalid,
          std::nullopt,
          fmt::format("starting input context {} is not available", key_text));
        continue;
      }
      auto context = content::HydrateInputContext(
        *context_asset, instantiator.Loader(), input);
      if (!context) {
        instantiator.Issue(asset, issue_codes::kInputContextInvalid,
          std::nullopt,
          fmt::format(
            "starting input context {} cannot be hydrated", key_text));
        continue;
      }
      input.AddMappingContext(context, record.priority);
      input.ActivateMappingContext(context);
    }
  }

  //! The node's `SceneInstanceComponent`; null when it is not an instance
  //! root.
  auto InstanceOf(scene::SceneNode& node)
    -> observer_ptr<scene::SceneInstanceComponent>
  {
    if (!node.IsAlive()) {
      return nullptr;
    }
    const auto impl = node.GetImpl();
    if (!impl.has_value()
      || !impl->get().HasComponent<scene::SceneInstanceComponent>()) {
      return nullptr;
    }
    return observer_ptr {
      &impl->get().GetComponent<scene::SceneInstanceComponent>(),
    };
  }

  //! An instance root above a node, and the node ID its source gave the next
  //! node down toward that node.
  struct OuterLevel {
    scene::SceneNode root;
    observer_ptr<scene::SceneInstanceComponent> component;
    data::NodeId id;
  };

  //! The instance roots whose instances bring `node` in, innermost first.
  /*!
   A node is brought in by the nearest instance root above it whose source
   defines it; a node no source defines, such as a host's node, has none.
  */
  auto OuterLevels(scene::SceneNode node) -> std::vector<OuterLevel>
  {
    auto levels = std::vector<OuterLevel> {};
    auto current = std::move(node);
    auto ancestor = current.GetParent();
    while (ancestor.has_value()) {
      const auto component = InstanceOf(*ancestor);
      const auto id = component != nullptr
        ? component->IdOf(current.GetHandle())
        : std::nullopt;
      if (id.has_value()) {
        levels.push_back({
          .root = *ancestor,
          .component = component,
          .id = *id,
        });
        current = *ancestor;
      }
      ancestor = ancestor->GetParent();
    }
    return levels;
  }

  //! The scene change a host's change encodes; none when its value is not a
  //! value of its property, or a parameter's node value names a node. A live
  //! node a `NodeParameterValue` names is found in `host_nodes`, or added.
  auto Encode(
    const InstanceChange& change, std::vector<scene::SceneNode>& host_nodes)
    -> std::optional<data::SceneChange>
  {
    return std::visit(
      [&change, &host_nodes]<typename Change>(
        const Change& value) -> std::optional<data::SceneChange> {
        if constexpr (std::is_same_v<Change, NodeParameterValue>) {
          if (!value.node.IsAlive()) {
            return std::nullopt;
          }
          const auto found = std::ranges::find(
            host_nodes, value.node.GetHandle(), &scene::SceneNode::GetHandle);
          const auto position
            = static_cast<std::uint32_t>(found - host_nodes.begin());
          if (found == host_nodes.end()) {
            host_nodes.push_back(value.node);
          }
          return data::SceneChange {
            .path = change.path,
            .change = data::ParameterChange {
              .slot_id = value.slot_id,
              .key = value.key,
              .value = data::ScriptNodeParam { .node_index = position },
            },
          };
        } else if constexpr (std::is_same_v<Change, data::ParameterChange>) {
          const auto* node = std::get_if<data::ScriptNodeParam>(&value.value);
          if (node != nullptr
            && node->node_index != data::pak::scripting::kUnsetNodeParam) {
            return std::nullopt;
          }
          return data::SceneChange { .path = change.path, .change = value };
        } else {
          const auto encoded
            = data::schema::EncodeChange(value.override_id, value.value);
          if (!encoded.has_value()) {
            return std::nullopt;
          }
          return data::SceneChange {
            .path = change.path,
            .change = world::OverrideRecord {
              .override_id = value.override_id,
              .part_id = value.part_id,
              .value = *encoded,
            },
          };
        }
      },
      change.change);
  }

  //! A node below a live instance root and the live instance roots that lead
  //! to it, outermost first.
  class LiveTarget final {
  public:
    //! The node `path` names below `root`; none when an element names no
    //! node, or a node before the last is not an instance root.
    static auto Resolve(scene::SceneNode& root,
      const std::span<const data::NodeId> path) -> std::optional<LiveTarget>
    {
      const auto component = InstanceOf(root);
      if (component == nullptr || path.empty()) {
        return std::nullopt;
      }
      auto target = LiveTarget {};
      auto suffix = std::vector<data::NodeId>(path.begin(), path.end());
      for (const auto& outer : OuterLevels(root)) {
        suffix.insert(suffix.begin(), outer.id);
        target.levels_.push_back({
          .root = outer.root,
          .component = outer.component,
          .path = suffix,
        });
      }
      std::ranges::reverse(target.levels_);
      target.levels_.push_back({
        .root = root,
        .component = component,
        .path = { path.begin(), path.end() },
      });
      for (std::size_t depth = 1U; depth < path.size(); ++depth) {
        auto nested = root.GetInstance().Find(path.first(depth));
        if (!nested.has_value()) {
          return std::nullopt;
        }
        const auto nested_component = InstanceOf(*nested);
        if (nested_component == nullptr) {
          return std::nullopt;
        }
        target.levels_.push_back({
          .root = std::move(*nested),
          .component = nested_component,
          .path
          = { path.begin() + static_cast<std::ptrdiff_t>(depth), path.end() },
        });
      }
      auto node = root.GetInstance().Find(path);
      if (!node.has_value()) {
        return std::nullopt;
      }
      target.node_ = std::move(*node);
      const auto nodes = target.Source()->GetNodes();
      const auto found
        = std::ranges::find(nodes, path.back(), &world::NodeRecord::node_id);
      if (found == nodes.end()) {
        return std::nullopt;
      }
      target.index_
        = static_cast<world::SceneNodeIndexT>(found - nodes.begin());
      return target;
    }

    //! The scene that defines the node.
    [[nodiscard]] auto Source() const
      -> const std::shared_ptr<const data::SceneAsset>&
    {
      return levels_.back().component->Source();
    }

    [[nodiscard]] auto Node() -> scene::SceneNode& { return node_; }
    [[nodiscard]] auto Index() const -> world::SceneNodeIndexT
    {
      return index_;
    }

    //! The occurrence of the node's scene in Compose().
    [[nodiscard]] auto Occurrence() const -> std::size_t
    {
      return levels_.size() - 1U;
    }

    //! A composition of the node's scene with the changes the live instance
    //! roots hold of the node: one occurrence per level, then the outermost
    //! level's requester, when it is a scene, then one occurrence per level
    //! that supplies that level's host changes.
    [[nodiscard]] auto Compose() const -> data::ComposedScene
    {
      auto composed = data::ComposedScene {};
      for (const auto [level, entry] : std::views::enumerate(levels_)) {
        composed.occurrences.push_back({
          .scene = entry.component->Source(),
          .level = static_cast<std::uint32_t>(level) + 1U,
        });
      }
      const auto& requester = levels_.front().component->Requester();
      if (requester) {
        composed.occurrences.push_back({ .scene = requester });
      }
      // A host supplies no scene: its values name no tables.
      for (std::size_t level = 0; level < levels_.size(); ++level) {
        composed.occurrences.push_back({});
      }
      auto reaching = std::vector<data::ReachingChange> {};
      for (const auto [level, entry] : std::views::enumerate(levels_)) {
        const auto index = static_cast<std::size_t>(level);
        for (const auto& change : entry.component->Changes()) {
          if (change.path != entry.path) {
            continue;
          }
          auto supplier = index == 0U ? levels_.size() : index - 1U;
          if (entry.component->IsHostChange(data::AddressOf(change))) {
            supplier = HostOccurrence(index);
          }
          reaching.push_back({
            .change = change,
            .supplier = supplier,
            .consumed = change.path.size() - 1U,
            .level = static_cast<std::uint32_t>(level),
          });
        }
      }
      composed.occurrences.at(Occurrence()).changes = std::move(reaching);
      composed.occurrences.at(Occurrence()).slots
        = data::ComposeSlots(composed, Occurrence());
      return composed;
    }

    //! The live nodes of each occurrence of Compose(), by node index: the
    //! levels' sources', the requester's and each level's host nodes.
    [[nodiscard]] auto Nodes() const
      -> std::vector<std::vector<scene::SceneNode>>
    {
      auto nodes = std::vector<std::vector<scene::SceneNode>> {};
      for (const auto& entry : levels_) {
        auto created = std::vector<scene::SceneNode> {};
        for (const auto& record : entry.component->Source()->GetNodes()) {
          const auto found = entry.root.GetInstance().Find(
            std::span<const data::NodeId>(&record.node_id, 1U));
          created.push_back(found.value_or(scene::SceneNode {}));
        }
        nodes.push_back(std::move(created));
      }
      const auto& front = *levels_.front().component;
      if (front.Requester()) {
        nodes.emplace_back(
          front.RequesterNodes().begin(), front.RequesterNodes().end());
      }
      for (const auto& entry : levels_) {
        const auto host = entry.component->HostNodes();
        nodes.emplace_back(host.begin(), host.end());
      }
      return nodes;
    }

  private:
    //! The occurrence that supplies the host's changes held at `level`.
    [[nodiscard]] auto HostOccurrence(const std::size_t level) const
      -> std::size_t
    {
      const auto requester = levels_.front().component->Requester() ? 1U : 0U;
      return levels_.size() + requester + level;
    }

    struct Level {
      scene::SceneNode root;
      observer_ptr<scene::SceneInstanceComponent> component;
      //! The node's path below this root.
      std::vector<data::NodeId> path;
    };

    LiveTarget() = default;

    std::vector<Level> levels_;
    scene::SceneNode node_;
    world::SceneNodeIndexT index_ = 0;
  };

  //! Applies the value `address` sets on the target node again.
  auto Reapply(LiveTarget& target, const data::ChangeAddress& address,
    Instantiator& instantiator,
    const observer_ptr<const scripting::SceneScriptAttachment> scripts,
    ChangeResult& result) -> void
  {
    auto composed = target.Compose();
    const auto occurrence = target.Occurrence();
    if (const auto* property
      = std::get_if<data::PropertyAddress>(&address.target)) {
      const auto node = data::ComposeNode(composed, occurrence, target.Index());
      instantiator.UseComposition(std::move(composed));
      instantiator.Reapply(occurrence, target.Node(), node, *property);
      return;
    }
    if (scripts == nullptr) {
      return;
    }
    const auto& parameter = std::get<data::ParameterAddress>(address.target);
    const auto nodes = target.Nodes();
    auto applied = scripts->ApplyParameter(composed, occurrence, nodes, {},
      target.Index(), parameter.slot_id, parameter.key);
    std::ranges::move(applied.slots, std::back_inserter(result.scripts));
  }

} // namespace

// The caller awaits at once and owns the referenced arguments.
auto Instantiate(SceneInstantiationRequest request,
  content::IAssetLoader& loader) -> co::Co<InstantiationResult>
{
  auto result = InstantiationResult {};
  if (!request.asset || request.target == nullptr) {
    co_return result;
  }
  const auto& asset = *request.asset;
  auto& target = *request.target;
  auto instantiator
    = Instantiator(loader, observer_ptr { &target }, request.scripts, result);

  ApplyEnvironment(instantiator, asset, target);
  co_await instantiator.Compose(request.asset);
  instantiator.Build();
  result.nodes = instantiator.RootNodes();
  try {
    target.GetDirectionalLightResolver().Validate();
  } catch (const std::exception& error) {
    instantiator.Issue(
      asset, issue_codes::kLightsInvalid, std::nullopt, error.what());
  }
  ApplyStartingCamera(instantiator, asset, target);
  if (request.input != nullptr) {
    co_await StartInputContexts(instantiator, asset, *request.input);
  }
  instantiator.AttachScripts();
  co_return result;
}

// The caller awaits at once and owns the referenced arguments.
auto InstantiateUnder(InstanceRequest request, content::IAssetLoader& loader)
  -> co::Co<InstantiationResult>
{
  auto result = InstantiationResult {};
  auto instantiator
    = Instantiator(loader, request.target, request.scripts, result);
  if (request.target == nullptr || !request.root.IsAlive()
    || !request.target->Contains(request.root)
    || InstanceOf(request.root) != nullptr) {
    instantiator.Issue(request.source, issue_codes::kInstanceRootInvalid,
      std::nullopt,
      "the node to instance under is not a live node of the scene, or is an "
      "instance root already");
    co_return result;
  }
  auto changes = std::vector<data::SceneChange> {};
  auto host_nodes = std::vector<scene::SceneNode> {};
  for (const auto& change : request.changes) {
    if (auto encoded = Encode(change, host_nodes); encoded.has_value()) {
      changes.push_back(std::move(*encoded));
      continue;
    }
    instantiator.Issue(data::AssetKey {},
      data::to_string(data::CompositionIssueCode::kChangeInvalid), std::nullopt,
      "the value is not a value of the property it changes", change.path);
  }
  // The scenes whose instances bring the root in: instancing one of them
  // again is a cycle.
  auto ancestors = std::vector<data::AssetKey> {};
  for (const auto& outer : OuterLevels(request.root)) {
    ancestors.push_back(outer.component->Source()->GetAssetKey());
    if (const auto& requester = outer.component->Requester()) {
      ancestors.push_back(requester->GetAssetKey());
    }
  }
  co_await instantiator.ComposeUnder(request.root, request.source,
    std::move(changes), std::move(host_nodes), std::move(ancestors));
  instantiator.Build();
  result.nodes = instantiator.RootNodes();
  try {
    request.target->GetDirectionalLightResolver().Validate();
  } catch (const std::exception& error) {
    instantiator.Issue(
      request.source, issue_codes::kLightsInvalid, std::nullopt, error.what());
  }
  instantiator.AttachScripts();
  co_return result;
}

auto SetChange(scene::SceneNode& root, const InstanceChange& change,
  content::IAssetLoader& loader,
  const observer_ptr<const scripting::SceneScriptAttachment> scripts)
  -> ChangeResult
{
  auto built = InstantiationResult {};
  auto instantiator = Instantiator(loader, nullptr, scripts, built);
  auto result = ChangeResult {};
  const auto finish = [&] -> ChangeResult {
    std::ranges::move(built.issues, std::back_inserter(result.issues));
    return std::move(result);
  };
  const auto component = InstanceOf(root);
  if (component == nullptr) {
    instantiator.Issue(data::AssetKey {}, issue_codes::kNotInstanceRoot,
      std::nullopt, "the node is not an instance root", change.path);
    return finish();
  }
  auto host_nodes = std::vector<scene::SceneNode>(
    component->HostNodes().begin(), component->HostNodes().end());
  auto encoded = Encode(change, host_nodes);
  if (!encoded.has_value()) {
    instantiator.Issue(data::AssetKey {},
      data::to_string(data::CompositionIssueCode::kChangeInvalid), std::nullopt,
      "the value is not a value of the property it changes", change.path);
    return finish();
  }
  auto target = LiveTarget::Resolve(root, encoded->path);
  const auto outcome = target.has_value()
    ? data::CheckChange(target->Source(), target->Index(), *encoded)
    : data::ChangeOutcome::kUnresolved;
  if (outcome != data::ChangeOutcome::kApplied) {
    instantiator.Issue(data::AssetKey {},
      outcome == data::ChangeOutcome::kInvalid
        ? data::to_string(data::CompositionIssueCode::kChangeInvalid)
        : issue_codes::kChangeUnresolved,
      std::nullopt,
      "the change names no node of the instance, or a property that node "
      "does not have, or a value its record does not accept",
      change.path);
    return finish();
  }
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - applied implies found
  const auto address = data::AddressOf(*encoded);
  component->SetHostNodes(std::move(host_nodes));
  component->SetChange(std::move(*encoded), true);
  Reapply(*target, address, instantiator, scripts, result);
  // NOLINTEND(bugprone-unchecked-optional-access)
  return finish();
}

auto ClearChange(scene::SceneNode& root, const data::ChangeAddress& address,
  content::IAssetLoader& loader,
  const observer_ptr<const scripting::SceneScriptAttachment> scripts)
  -> ChangeResult
{
  auto built = InstantiationResult {};
  auto instantiator = Instantiator(loader, nullptr, scripts, built);
  auto result = ChangeResult {};
  const auto finish = [&] -> ChangeResult {
    std::ranges::move(built.issues, std::back_inserter(result.issues));
    return std::move(result);
  };
  const auto component = InstanceOf(root);
  if (component == nullptr) {
    instantiator.Issue(data::AssetKey {}, issue_codes::kNotInstanceRoot,
      std::nullopt, "the node is not an instance root", address.path);
    return finish();
  }
  auto target = LiveTarget::Resolve(root, address.path);
  if (!target.has_value() || !component->ClearChange(address)) {
    instantiator.Issue(data::AssetKey {}, issue_codes::kChangeUnresolved,
      std::nullopt, "the instance holds no change at the address",
      address.path);
    return finish();
  }
  Reapply(*target, address, instantiator, scripts, result);
  return finish();
}

auto Detach(scene::Scene& target, scene::SceneNode& root) -> bool
{
  const auto component = InstanceOf(root);
  if (component == nullptr || !target.Contains(root)) {
    return false;
  }
  auto roots = std::vector<scene::SceneNode> {};
  for (const auto [index, record] :
    std::views::enumerate(component->Source()->GetNodes())) {
    if (std::cmp_not_equal(record.parent_index, index)) {
      continue;
    }
    const auto handle = component->FindNode(record.node_id);
    if (!handle.has_value()) {
      continue;
    }
    if (auto node = target.GetNode(*handle); node.has_value()) {
      roots.push_back(std::move(*node));
    }
  }
  (void)target.DestroyNodeHierarchies(roots);
  if (const auto impl = root.GetImpl(); impl.has_value()) {
    impl->get().RemoveComponent<scene::SceneInstanceComponent>();
  }
  return true;
}

} // namespace oxygen::instantiation
