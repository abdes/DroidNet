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
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
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
    Instantiator(content::IAssetLoader& loader, scene::Scene& target,
      const observer_ptr<const scripting::SceneScriptAttachment> scripts,
      InstantiationResult& result)
      : loader_(&loader)
      , target_(&target)
      , scripts_(scripts)
      , result_(&result)
    {
    }

    //! Loads `asset` and every scene reachable through its instances, then
    //! composes it.
    // The caller awaits at once and owns the referenced arguments.
    auto Compose(std::shared_ptr<const data::SceneAsset> asset) -> co::Co<>
    {
      auto loaded = std::unordered_map<data::AssetKey,
        std::shared_ptr<const data::SceneAsset>> {};
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
      composed_ = data::ComposeScene(std::move(asset),
        [&loaded](const data::AssetKey& key)
          -> std::shared_ptr<const data::SceneAsset> {
          const auto found = loaded.find(key);
          return found != loaded.end() ? found->second : nullptr;
        });
      ReportComposition();
    }

    //! Creates the nodes of every occurrence, parents first, and makes each
    //! instance root an instance.
    auto Build() -> void
    {
      const auto& occurrences = composed_.occurrences;
      nodes_.resize(occurrences.size());
      for (std::size_t index = 0; index < occurrences.size(); ++index) {
        BuildOccurrence(index);
      }
      for (std::size_t index = 1; index < occurrences.size(); ++index) {
        AttachInstance(index);
      }
    }

    //! The nodes created for the requested scene.
    [[nodiscard]] auto RootNodes() const -> const std::vector<scene::SceneNode>&
    {
      return nodes_.front();
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
        auto attached = scripts_->Attach(composed_, occurrence, nodes_);
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
      LOG_F(WARNING, "scene {}{}: {} ({})",
        data::to_string(asset.GetAssetKey()),
        node_index.has_value() ? fmt::format(" node {}", *node_index)
                               : std::string {},
        message, code);
      result_->issues.push_back({
        .code = std::string(code),
        .scene = asset.GetAssetKey(),
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
    [[nodiscard]] auto SceneOf(const std::size_t occurrence) const
      -> const data::SceneAsset&
    {
      return *composed_.occurrences.at(occurrence).scene;
    }

    //! The scene that supplied a value: an occurrence's scene.
    [[nodiscard]] auto SupplierOf(const data::ChangeSupplier supplier) const
      -> const data::SceneAsset&
    {
      return SceneOf(supplier);
    }

    auto ReportComposition() -> void
    {
      for (const auto& issue : composed_.issues) {
        const auto& scene = SceneOf(issue.occurrence);
        Issue(scene, data::to_string(issue.code), issue.node_index,
          issue.scene.IsNil()
            ? std::string { "a record of the scene is invalid" }
            : fmt::format("instanced scene {}", data::to_string(issue.scene)));
      }
      for (const auto& unresolved : composed_.unresolved) {
        Issue(SupplierOf(unresolved.supplier), issue_codes::kChangeUnresolved,
          unresolved.instance_root,
          "the change names no node of the instanced scene, or a property "
          "that node does not have",
          unresolved.change.path);
      }
    }

    auto BuildOccurrence(const std::size_t index) -> void
    {
      const auto& occurrence = composed_.occurrences.at(index);
      const auto& asset = *occurrence.scene;
      auto parent = std::optional<scene::SceneNode> {};
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

    //! Applies the composed records of one node; applying them again
    //! replaces what they set.
    auto ApplyComponents(const std::size_t occurrence, scene::SceneNode& node,
      const data::ComposedNode& composed) -> void
    {
      const auto& asset = SceneOf(occurrence);
      const auto check
        = [&](const bool applied, const std::string_view kind) -> void {
        if (!applied) {
          Issue(asset, issue_codes::kComponentNotAttached, composed.index,
            fmt::format("cannot attach the {}", kind));
        }
      };
      if (composed.renderable.has_value()) {
        ApplyRenderable(asset, node, composed);
      }
      if (composed.perspective_camera.has_value()) {
        check(ApplyCamera(node, *composed.perspective_camera), "camera");
      }
      if (composed.orthographic_camera.has_value()) {
        check(ApplyCamera(node, *composed.orthographic_camera), "camera");
      }
      if (composed.directional_light.has_value()) {
        check(ApplyLight(node, *composed.directional_light), "light");
      }
      if (composed.point_light.has_value()) {
        check(ApplyLight(node, *composed.point_light), "light");
      }
      if (composed.spot_light.has_value()) {
        check(ApplyLight(node, *composed.spot_light), "light");
      }
      if (composed.local_fog_volume.has_value()) {
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
      const auto& supplier = SupplierOf(entry.supplier);
      const auto* property
        = data::schema::FindOverrideProperty(record.override_id);
      DCHECK_NOTNULL_F(property);
      if (property->part_kind == data::schema::PartKind::kSurface
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
        auto material
          = loader_->GetMaterialAsset(record.value.as_asset, supplier);
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
        const auto policies
          = supplier.GetComponents<data::pak::geometry::LodPolicyRecord>();
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

    //! Gives the instance root of `occurrence` its `SceneInstanceComponent`.
    auto AttachInstance(const std::size_t index) -> void
    {
      const auto& occurrence = composed_.occurrences.at(index);
      if (!occurrence.parent.has_value()) {
        return;
      }
      const auto parent = *occurrence.parent;
      auto& root = nodes_.at(parent).at(occurrence.instance_root);
      auto changes = std::vector<data::SceneChange> {};
      for (const auto& reaching : occurrence.changes) {
        if (reaching.supplier == parent && reaching.consumed == 0U) {
          changes.push_back(reaching.change);
        }
      }
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
      root_impl.AddComponent<scene::SceneInstanceComponent>(
        occurrence.scene, std::move(changes), std::move(created));
    }

    observer_ptr<content::IAssetLoader> loader_;
    observer_ptr<scene::Scene> target_;
    observer_ptr<const scripting::SceneScriptAttachment> scripts_;
    observer_ptr<InstantiationResult> result_;
    data::ComposedScene composed_;
    //! The nodes created for each occurrence, by node index.
    std::vector<std::vector<scene::SceneNode>> nodes_;
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
  auto instantiator = Instantiator(loader, target, request.scripts, result);

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

} // namespace oxygen::instantiation
