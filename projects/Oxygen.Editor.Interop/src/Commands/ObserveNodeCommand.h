//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <span>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include <Commands/DirectionalLightPropertyApplier.h>
#include <Commands/PerspectiveCameraPropertyApplier.h>
#include <Commands/TransformPropertyApplier.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/NodeRegistry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

//! Stored node properties and resolved LOD-zero assets at one mutation boundary.
struct NodeObservation {
  bool exists = false;
  bool primary_sun = false;
  std::vector<PropertyEntry> properties;
  std::string geometry_key;
  std::string geometry_name;
  std::uint64_t vertex_count = 0;
  std::uint64_t index_count = 0;
  std::vector<std::string> material_keys;
  std::vector<std::array<float, 4>> material_base_colors;
};

//! Looks up the authored node identity when its queued observation executes.
class ObserveNodeCommand final : public EditorCommand {
public:
  ObserveNodeCommand(UuidKey node, std::function<void(NodeObservation)> complete)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , node_(node)
    , complete_(std::move(complete)) {}

  void Execute(CommandContext& context) override
  {
    NodeObservation result;
    const auto handle = NodeRegistry::Lookup(node_);
    if (context.Scene && handle) {
      if (auto node = context.Scene->GetNode(*handle); node && node->IsAlive()) {
        result.exists = true;
        ReadTransform(*node, result);
        ReadCamera(*node, result);
        ReadLight(*node, result);
        ReadGeometry(*node, result);
        const auto sun = context.Scene->GetDirectionalLightResolver().ResolvePrimarySun();
        result.primary_sun = sun && sun->NodeHandle() == *handle;
      }
    }
    complete_(std::move(result));
  }

private:
  template <typename TField, typename TValue>
  static void Add(NodeObservation& result, ComponentId component, TField field, TValue value)
  {
    result.properties.push_back({ component, static_cast<std::uint16_t>(field),
      static_cast<float>(value) });
  }

  static void ReadTransform(scene::SceneNode& node, NodeObservation& result)
  {
    const auto transform = node.GetTransform();
    if (const auto position = transform.GetLocalPosition()) {
      Add(result, ComponentId::kTransform, TransformField::kPositionX, position->x);
      Add(result, ComponentId::kTransform, TransformField::kPositionY, position->y);
      Add(result, ComponentId::kTransform, TransformField::kPositionZ, position->z);
    }
    if (const auto rotation = transform.GetLocalRotation()) {
      const auto angles = oxygen::interop::rotation::ToEulerDegrees(*rotation);
      Add(result, ComponentId::kTransform, TransformField::kRotationXDegrees, angles.x);
      Add(result, ComponentId::kTransform, TransformField::kRotationYDegrees, angles.y);
      Add(result, ComponentId::kTransform, TransformField::kRotationZDegrees, angles.z);
    }
    if (const auto scale = transform.GetLocalScale()) {
      Add(result, ComponentId::kTransform, TransformField::kScaleX, scale->x);
      Add(result, ComponentId::kTransform, TransformField::kScaleY, scale->y);
      Add(result, ComponentId::kTransform, TransformField::kScaleZ, scale->z);
    }
  }

  static void ReadCamera(scene::SceneNode& node, NodeObservation& result)
  {
    if (const auto camera = node.GetCameraAs<scene::PerspectiveCamera>()) {
      const auto& value = camera->get();
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kFieldOfViewYRadians, value.GetFieldOfView());
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kAspectRatio, value.GetAspectRatio());
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kNearPlane, value.GetNearPlane());
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kFarPlane, value.GetFarPlane());
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kApertureF, value.Exposure().aperture_f);
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kShutterRate, value.Exposure().shutter_rate);
      Add(result, ComponentId::kPerspectiveCamera, PerspectiveCameraField::kIso, value.Exposure().iso);
    }
  }

  static void ReadLight(scene::SceneNode& node, NodeObservation& result)
  {
    const auto reference = node.GetLightAs<scene::DirectionalLight>();
    if (!reference) {
      return;
    }
    const auto& light = reference->get();
    const auto& common = light.Common();
    const auto& csm = light.CascadedShadows();
    const auto add = [&result](DirectionalLightField field, auto value) {
      Add(result, ComponentId::kDirectionalLight, field, value);
    };
    add(DirectionalLightField::kColorR, common.color_rgb.r);
    add(DirectionalLightField::kColorG, common.color_rgb.g);
    add(DirectionalLightField::kColorB, common.color_rgb.b);
    add(DirectionalLightField::kAffectsWorld, common.affects_world);
    add(DirectionalLightField::kMobility, common.mobility);
    add(DirectionalLightField::kCastsShadows, common.casts_shadows);
    add(DirectionalLightField::kShadowBias, common.shadow.bias);
    add(DirectionalLightField::kShadowNormalBias, common.shadow.normal_bias);
    add(DirectionalLightField::kContactShadows, common.shadow.contact_shadows);
    add(DirectionalLightField::kShadowResolutionHint, common.shadow.resolution_hint);
    add(DirectionalLightField::kExposureCompensation, common.exposure_compensation_ev);
    add(DirectionalLightField::kIntensityLux, light.GetIntensityLux());
    add(DirectionalLightField::kAngularSizeRadians, light.GetAngularSizeRadians());
    add(DirectionalLightField::kEnvironmentContribution, light.GetEnvironmentContribution());
    add(DirectionalLightField::kIsSunLight, light.IsSunLight());
    add(DirectionalLightField::kCascadeCount, csm.cascade_count);
    add(DirectionalLightField::kSplitMode, csm.split_mode);
    add(DirectionalLightField::kMaxShadowDistance, csm.max_shadow_distance);
    add(DirectionalLightField::kCascadeDistance0, csm.cascade_distances[0]);
    add(DirectionalLightField::kCascadeDistance1, csm.cascade_distances[1]);
    add(DirectionalLightField::kCascadeDistance2, csm.cascade_distances[2]);
    add(DirectionalLightField::kCascadeDistance3, csm.cascade_distances[3]);
    add(DirectionalLightField::kDistributionExponent, csm.distribution_exponent);
    add(DirectionalLightField::kTransitionFraction, csm.transition_fraction);
    add(DirectionalLightField::kDistanceFadeoutFraction, csm.distance_fadeout_fraction);
  }

  static void ReadGeometry(scene::SceneNode& node, NodeObservation& result)
  {
    const auto renderable = node.GetRenderable();
    if (!renderable.HasGeometry()) {
      return;
    }
    const auto geometry = renderable.GetGeometry();
    if (!geometry) {
      return;
    }
    result.geometry_key = data::to_string(geometry->GetAssetKey());
    result.geometry_name = geometry->GetAssetName();
    if (const auto mesh = geometry->MeshAt(0)) {
      result.vertex_count = mesh->VertexCount();
      result.index_count = mesh->IndexCount();
      for (std::size_t slot = 0; slot < mesh->SubMeshes().size(); ++slot) {
        const auto material = renderable.ResolveSubmeshMaterial(0, slot);
        result.material_keys.push_back(material
          ? data::to_string(material->GetAssetKey()) : std::string {});
        const auto color = material ? material->GetBaseColor() : std::span<const float, 4>{fallback_color_};
        result.material_base_colors.push_back({color[0], color[1], color[2], color[3]});
      }
    }
  }

  UuidKey node_;
  static constexpr std::array<float, 4> fallback_color_{};
  std::function<void(NodeObservation)> complete_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
