//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <lua.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto MobilityToString(const scene::LightMobility mobility) -> const char*
  {
    switch (mobility) {
    case scene::LightMobility::kRealtime:
      return "realtime";
    case scene::LightMobility::kMixed:
      return "mixed";
    case scene::LightMobility::kBaked:
      return "baked";
    }
    return "realtime";
  }

  auto TryParseMobility(const std::string_view value, scene::LightMobility& out)
    -> bool
  {
    if (value == "realtime") {
      out = scene::LightMobility::kRealtime;
      return true;
    }
    if (value == "mixed") {
      out = scene::LightMobility::kMixed;
      return true;
    }
    if (value == "baked") {
      out = scene::LightMobility::kBaked;
      return true;
    }
    return false;
  }

  auto ShadowResolutionHintToString(const scene::ShadowResolutionHint value)
    -> const char*
  {
    switch (value) {
    case scene::ShadowResolutionHint::kLow:
      return "low";
    case scene::ShadowResolutionHint::kMedium:
      return "medium";
    case scene::ShadowResolutionHint::kHigh:
      return "high";
    case scene::ShadowResolutionHint::kUltra:
      return "ultra";
    }
    return "medium";
  }

  auto AtmosphereLightSlotName(const scene::AtmosphereLightSlot slot) -> const
    char*
  {
    switch (slot) {
    case scene::AtmosphereLightSlot::kPrimary:
      return "primary";
    case scene::AtmosphereLightSlot::kSecondary:
      return "secondary";
    default:
      return "none";
    }
  }

  auto TryParseAtmosphereLightSlot(const std::string_view name)
    -> std::optional<scene::AtmosphereLightSlot>
  {
    if (name == "primary") {
      return scene::AtmosphereLightSlot::kPrimary;
    }
    if (name == "secondary") {
      return scene::AtmosphereLightSlot::kSecondary;
    }
    if (name == "none") {
      return scene::AtmosphereLightSlot::kNone;
    }
    return std::nullopt;
  }

  auto TryParseShadowResolutionHint(
    const std::string_view value, scene::ShadowResolutionHint& out) -> bool
  {
    if (value == "low") {
      out = scene::ShadowResolutionHint::kLow;
      return true;
    }
    if (value == "medium") {
      out = scene::ShadowResolutionHint::kMedium;
      return true;
    }
    if (value == "high") {
      out = scene::ShadowResolutionHint::kHigh;
      return true;
    }
    if (value == "ultra") {
      out = scene::ShadowResolutionHint::kUltra;
      return true;
    }
    return false;
  }

  template <typename Fn> auto WithDirectional(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      return false;
    }
    auto light = node->GetLightAs<scene::DirectionalLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithPoint(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      return false;
    }
    auto light = node->GetLightAs<scene::PointLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithSpot(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      return false;
    }
    auto light = node->GetLightAs<scene::SpotLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithAny(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      return false;
    }

    if (auto light = node->GetLightAs<scene::DirectionalLight>();
      light.has_value()) {
      std::forward<Fn>(fn)(light->get());
      return true;
    }
    if (auto light = node->GetLightAs<scene::PointLight>(); light.has_value()) {
      std::forward<Fn>(fn)(light->get());
      return true;
    }
    if (auto light = node->GetLightAs<scene::SpotLight>(); light.has_value()) {
      std::forward<Fn>(fn)(light->get());
      return true;
    }
    return false;
  }

  template <typename T, typename Fn>
  auto EditTyped(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    return node != nullptr && node->EditLight<T>(std::forward<Fn>(fn));
  }
  template <typename Fn> auto EditDirectional(lua_State* state, Fn&& fn) -> bool
  {
    return EditTyped<scene::DirectionalLight>(state, std::forward<Fn>(fn));
  }
  template <typename Fn> auto EditPoint(lua_State* state, Fn&& fn) -> bool
  {
    return EditTyped<scene::PointLight>(state, std::forward<Fn>(fn));
  }
  template <typename Fn> auto EditSpot(lua_State* state, Fn&& fn) -> bool
  {
    return EditTyped<scene::SpotLight>(state, std::forward<Fn>(fn));
  }
  template <typename Fn> auto EditAny(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (!node) {
      return false;
    }
    if (node->GetLightAs<scene::DirectionalLight>()) {
      return EditDirectional(state, std::forward<Fn>(fn));
    }
    if (node->GetLightAs<scene::PointLight>()) {
      return EditPoint(state, std::forward<Fn>(fn));
    }
    if (node->GetLightAs<scene::SpotLight>()) {
      return EditSpot(state, std::forward<Fn>(fn));
    }
    return false;
  }

  auto TryGetNumberField(lua_State* state, const int table_index,
    const char* key, float& out) -> bool
  {
    lua_getfield(state, table_index, key);
    const bool ok = lua_isnumber(state, -1) != 0;
    if (ok) {
      out = static_cast<float>(lua_tonumber(state, -1));
    }
    lua_pop(state, 1);
    return ok;
  }

  auto TryGetBoolField(
    lua_State* state, const int table_index, const char* key, bool& out) -> bool
  {
    lua_getfield(state, table_index, key);
    const bool ok = lua_isboolean(state, -1) != 0;
    if (ok) {
      out = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);
    return ok;
  }

  auto TryGetVec3Field(
    lua_State* state, const int table_index, const char* key, Vec3& out) -> bool
  {
    lua_getfield(state, table_index, key);
    const bool ok = lua_isvector(state, -1) != 0;
    if (ok) {
      const float* v = lua_tovector(state, -1);
      out = Vec3 { v[0], v[1], v[2] }; // NOLINT
    }
    lua_pop(state, 1);
    return ok;
  }

  auto ApplyCommon(lua_State* state, const int table_index,
    scene::CommonLightProperties& common) -> void
  {
    bool b = false;
    float n = 0.0F;
    Vec3 v {};
    if (TryGetBoolField(state, table_index, "affects_world", b)) {
      common.affects_world = b;
    }
    if (TryGetVec3Field(state, table_index, "color_rgb", v)) {
      common.color_rgb = v;
    }
    lua_getfield(state, table_index, "mobility");
    size_t len = 0;
    const char* s = lua_tolstring(state, -1, &len);
    if (s != nullptr) {
      scene::LightMobility mobility = common.mobility;
      if (TryParseMobility(std::string_view(s, len), mobility)) {
        common.mobility = mobility;
      }
    }
    lua_pop(state, 1);
    if (TryGetBoolField(state, table_index, "casts_shadows", b)) {
      common.casts_shadows = b;
    }
    if (TryGetNumberField(state, table_index, "exposure_compensation_ev", n)) {
      common.exposure_compensation_ev = n;
    }
  }

  auto ValidLightTable(lua_State* state, int table) -> bool
  {
    if (lua_type(state, table) != LUA_TTABLE) {
      return false;
    }
    for (const char* key : {
           "attenuation_model",
           "decay_exponent",
           "environment_contribution",
           "is_sun_light",
         }) {
      lua_getfield(state, table, key);
      const bool obsolete = lua_type(state, -1) != LUA_TNIL;
      lua_pop(state, 1);
      if (obsolete) {
        return false;
      }
    }
    for (const char* key : {
           "range",
           "source_radius",
           "luminous_flux_lm",
           "intensity_lux",
           "angular_size_radians",
           "inner_cone_angle_radians",
           "outer_cone_angle_radians",
           "exposure_compensation_ev",
         }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      lua_pop(state, 1);
      if (type != LUA_TNIL && type != LUA_TNUMBER) {
        return false;
      }
    }
    for (const char* key : {
           "affects_world",
           "casts_shadows",
           "use_per_pixel_atmosphere_transmittance",
         }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      lua_pop(state, 1);
      if (type != LUA_TNIL && type != LUA_TBOOLEAN) {
        return false;
      }
    }
    for (const char* key :
      { "color_rgb", "atmosphere_disk_luminance_scale_rgb" }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      lua_pop(state, 1);
      if (type != LUA_TNIL && type != LUA_TVECTOR) {
        return false;
      }
    }
    lua_getfield(state, table, "mobility");
    const auto mobility_type = lua_type(state, -1);
    scene::LightMobility mobility {};
    const auto mobility_valid = mobility_type == LUA_TNIL
      || (mobility_type == LUA_TSTRING
        && TryParseMobility(lua_tostring(state, -1), mobility));
    lua_pop(state, 1);
    return mobility_valid;
  }

  auto ReadShadow(lua_State* state, int table, scene::ShadowSettings& shadow)
    -> bool
  {
    for (const auto& [key, value] : {
           std::pair { "bias", &shadow.bias },
           std::pair { "normal_bias", &shadow.normal_bias },
         }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      if (type == LUA_TNUMBER) {
        *value = static_cast<float>(lua_tonumber(state, -1));
      }
      lua_pop(state, 1);
      if (type != LUA_TNIL && type != LUA_TNUMBER) {
        return false;
      }
    }
    lua_getfield(state, table, "contact_shadows");
    const auto type = lua_type(state, -1);
    if (type == LUA_TBOOLEAN) {
      shadow.contact_shadows = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);
    if (type != LUA_TNIL && type != LUA_TBOOLEAN) {
      return false;
    }
    lua_getfield(state, table, "resolution_hint");
    bool valid = lua_type(state, -1) == LUA_TNIL;
    if (lua_type(state, -1) == LUA_TSTRING) {
      valid = TryParseShadowResolutionHint(
        lua_tostring(state, -1), shadow.resolution_hint);
    }
    lua_pop(state, 1);
    return valid;
  }

  auto ReadCsm(lua_State* state, int table, scene::CascadedShadowSettings& csm)
    -> bool
  {
    for (const auto& [key, value] : {
           std::pair { "max_shadow_distance", &csm.max_shadow_distance },
           std::pair { "distribution_exponent", &csm.distribution_exponent },
           std::pair { "transition_fraction", &csm.transition_fraction },
           std::pair {
             "distance_fadeout_fraction", &csm.distance_fadeout_fraction },
         }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      if (type == LUA_TNUMBER) {
        *value = static_cast<float>(lua_tonumber(state, -1));
      }
      lua_pop(state, 1);
      if (type != LUA_TNIL && type != LUA_TNUMBER) {
        return false;
      }
    }
    for (const char* key : { "cascade_count", "split_mode" }) {
      lua_getfield(state, table, key);
      const auto type = lua_type(state, -1);
      const auto number = type == LUA_TNUMBER ? lua_tonumber(state, -1) : 0.0;
      lua_pop(state, 1);
      if (type == LUA_TNIL) {
        continue;
      }
      if (type != LUA_TNUMBER || !std::isfinite(number) || number < 0
        || number > 4 || std::floor(number) != number) {
        return false;
      }
      if (std::string_view(key) == "cascade_count") {
        csm.cascade_count = static_cast<std::uint32_t>(number);
      } else {
        csm.split_mode = static_cast<scene::DirectionalCsmSplitMode>(
          static_cast<unsigned>(number));
      }
    }
    lua_getfield(state, table, "cascade_distances");
    if (lua_type(state, -1) == LUA_TNIL) {
      lua_pop(state, 1);
      return true;
    }
    if (lua_type(state, -1) != LUA_TTABLE || lua_objlen(state, -1) != 4) {
      lua_pop(state, 1);
      return false;
    }
    for (int i = 0; i < 4; ++i) {
      lua_rawgeti(state, -1, i + 1);
      const auto type = lua_type(state, -1);
      if (type == LUA_TNUMBER) {
        csm.cascade_distances[i] = static_cast<float>(lua_tonumber(state, -1));
      }
      lua_pop(state, 1);
      if (type != LUA_TNUMBER) {
        lua_pop(state, 1);
        return false;
      }
    }
    lua_pop(state, 1);
    return true;
  }

  template <typename T>
  auto ApplyLightPatch(lua_State* state, int table, T& light) -> bool
  {
    if (!ValidLightTable(state, table)) {
      return false;
    }
    ApplyCommon(state, table, light.Common());
    lua_getfield(state, table, "shadow");
    const auto shadow_type = lua_type(state, -1);
    const bool shadow_ok = shadow_type == LUA_TNIL
      || (shadow_type == LUA_TTABLE
        && ReadShadow(state, lua_gettop(state), light.Common().shadow));
    lua_pop(state, 1);
    if (!shadow_ok) {
      return false;
    }
    float number = NAN;
    if constexpr (std::is_same_v<T, scene::DirectionalLight>) {
      if (TryGetNumberField(state, table, "intensity_lux", number)) {
        light.SetIntensityLux(number);
      }
      if (TryGetNumberField(state, table, "angular_size_radians", number)) {
        light.SetAngularSizeRadians(number);
      }
      bool enabled = false;
      if (TryGetBoolField(
            state, table, "use_per_pixel_atmosphere_transmittance", enabled)) {
        light.SetUsePerPixelAtmosphereTransmittance(enabled);
      }
      Vec3 scale;
      if (TryGetVec3Field(
            state, table, "atmosphere_disk_luminance_scale_rgb", scale)) {
        light.SetAtmosphereDiskLuminanceScale(scale);
      }
      lua_getfield(state, table, "atmosphere_light_slot");
      const auto type = lua_type(state, -1);
      const auto slot = type == LUA_TSTRING
        ? std::string_view(lua_tostring(state, -1))
        : std::string_view {};
      lua_pop(state, 1);
      if (type != LUA_TNIL) {
        if (slot == "none") {
          light.SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kNone);
        } else if (slot == "primary") {
          light.SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
        } else if (slot == "secondary") {
          light.SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kSecondary);
        } else {
          return false;
        }
      }
      if (!ReadCsm(state, table, light.CascadedShadows())) {
        return false;
      }
    } else {
      if (TryGetNumberField(state, table, "range", number)) {
        light.SetRange(number);
      }
      if (TryGetNumberField(state, table, "source_radius", number)) {
        light.SetSourceRadius(number);
      }
      if (TryGetNumberField(state, table, "luminous_flux_lm", number)) {
        light.SetLuminousFluxLm(number);
      }
      if constexpr (std::is_same_v<T, scene::SpotLight>) {
        if (TryGetNumberField(
              state, table, "inner_cone_angle_radians", number)) {
          light.SetInnerConeAngleRadians(number);
        }
        if (TryGetNumberField(
              state, table, "outer_cone_angle_radians", number)) {
          light.SetOuterConeAngleRadians(number);
        }
      }
    }
    return true;
  }

  template <typename T, typename Patch>
  auto PatchTyped(lua_State* state, Patch&& patch) -> bool
  {
    return EditTyped<T>(state, std::forward<Patch>(patch));
  }
  template <typename Patch>
  auto PatchAny(lua_State* state, Patch&& patch) -> bool
  {
    return PatchTyped<scene::DirectionalLight>(state, patch)
      || PatchTyped<scene::PointLight>(state, patch)
      || PatchTyped<scene::SpotLight>(state, std::forward<Patch>(patch));
  }

  auto SceneNodeLightUpdate(lua_State* state) -> int
  {
    lua_pushboolean(state,
      static_cast<int>(PatchAny(state,
        [state](auto& light) { return ApplyLightPatch(state, 2, light); })));
    return 1;
  }

  auto SceneNodeLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!node->HasLight()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeAttachDirectionalLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    auto light = std::make_unique<scene::DirectionalLight>();
    const auto type = lua_type(state, 2);
    const bool valid = type == LUA_TNONE || type == LUA_TNIL
      || (type == LUA_TTABLE && ApplyLightPatch(state, 2, *light));
    const bool attached
      = node != nullptr && valid && node->AttachLight(std::move(light));
    lua_pushboolean(state, attached ? 1 : 0);
    return 1;
  }

  auto SceneNodeAttachPointLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    auto light = std::make_unique<scene::PointLight>();
    const auto type = lua_type(state, 2);
    const bool valid = type == LUA_TNONE || type == LUA_TNIL
      || (type == LUA_TTABLE && ApplyLightPatch(state, 2, *light));
    const bool attached
      = node != nullptr && valid && node->AttachLight(std::move(light));
    lua_pushboolean(state, attached ? 1 : 0);
    return 1;
  }

  auto SceneNodeAttachSpotLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    auto light = std::make_unique<scene::SpotLight>();
    const auto type = lua_type(state, 2);
    const bool valid = type == LUA_TNONE || type == LUA_TNIL
      || (type == LUA_TTABLE && ApplyLightPatch(state, 2, *light));
    const bool attached
      = node != nullptr && valid && node->AttachLight(std::move(light));
    lua_pushboolean(state, attached ? 1 : 0);
    return 1;
  }

  auto SceneNodeDetachLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->DetachLight() ? 1 : 0);
    return 1;
  }

  auto SceneNodeHasLight(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->HasLight() ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightType(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (node->GetLightAs<scene::DirectionalLight>().has_value()) {
      lua_pushliteral(state, "directional");
      return 1;
    }
    if (node->GetLightAs<scene::PointLight>().has_value()) {
      lua_pushliteral(state, "point");
      return 1;
    }
    if (node->GetLightAs<scene::SpotLight>().has_value()) {
      lua_pushliteral(state, "spot");
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeLightGetAffectsWorld(lua_State* state) -> int
  {
    bool v = false;
    if (!WithAny(state, [&v](auto& l) { v = l.Common().affects_world; })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushboolean(state, v ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightSetAffectsWorld(lua_State* state) -> int
  {
    const int entry_top = lua_gettop(state);
    if (entry_top < 2) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TBOOLEAN) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      EditAny(state, [v](auto& l) { l.Common().affects_world = v; }) ? 1 : 0);
    CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
    return 1;
  }

  auto SceneNodeLightGetColorRgb(lua_State* state) -> int
  {
    Vec3 v {};
    if (!WithAny(state, [&v](auto& l) { v = l.Common().color_rgb; })) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, v);
  }

  auto SceneNodeLightSetColorRgb(lua_State* state) -> int
  {
    Vec3 v {};
    if (!TryCheckVec3(state, 2, v)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state,
      EditAny(state, [v](auto& l) { l.Common().color_rgb = v; }) ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightGetMobility(lua_State* state) -> int
  {
    const char* text = nullptr;
    if (!WithAny(state,
          [&text](auto& l) { text = MobilityToString(l.Common().mobility); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushstring(state, text);
    return 1;
  }

  auto SceneNodeLightSetMobility(lua_State* state) -> int
  {
    size_t len = 0;
    const char* text = lua_tolstring(state, 2, &len);
    if (text == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    scene::LightMobility value {};
    if (!TryParseMobility(std::string_view(text, len), value)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state,
      EditAny(state, [value](auto& l) { l.Common().mobility = value; }) ? 1
                                                                        : 0);
    return 1;
  }

  auto SceneNodeLightGetCastsShadows(lua_State* state) -> int
  {
    bool v = false;
    if (!WithAny(state, [&v](auto& l) { v = l.Common().casts_shadows; })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushboolean(state, v ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightSetCastsShadows(lua_State* state) -> int
  {
    if (lua_type(state, 2) != LUA_TBOOLEAN) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      EditAny(state, [v](auto& l) { l.Common().casts_shadows = v; }) ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightGetExposureCompensationEv(lua_State* state) -> int
  {
    float v = 0.0F;
    if (!WithAny(
          state, [&v](auto& l) { v = l.Common().exposure_compensation_ev; })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, v);
    return 1;
  }

  auto SceneNodeLightSetExposureCompensationEv(lua_State* state) -> int
  {
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto v = static_cast<float>(lua_tonumber(state, 2));
    lua_pushboolean(state,
      EditAny(state, [v](auto& l) { l.Common().exposure_compensation_ev = v; })
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeLightGetShadowSettings(lua_State* state) -> int
  {
    scene::ShadowSettings settings {};
    if (!WithAny(state,
          [&settings](auto& light) { settings = light.Common().shadow; })) {
      lua_pushnil(state);
      return 1;
    }

    lua_createtable(state, 0, 4);
    lua_pushnumber(state, settings.bias);
    lua_setfield(state, -2, "bias");
    lua_pushnumber(state, settings.normal_bias);
    lua_setfield(state, -2, "normal_bias");
    lua_pushboolean(state, settings.contact_shadows ? 1 : 0);
    lua_setfield(state, -2, "contact_shadows");
    lua_pushstring(
      state, ShadowResolutionHintToString(settings.resolution_hint));
    lua_setfield(state, -2, "resolution_hint");
    return 1;
  }

  auto SceneNodeLightSetShadowSettings(lua_State* state) -> int
  {
    const bool ok = lua_type(state, 2) == LUA_TTABLE
      && PatchAny(state, [state](auto& light) {
           return ReadShadow(state, 2, light.Common().shadow);
         });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  auto SceneNodeLightGetCascadedShadows(lua_State* state) -> int
  {
    scene::CascadedShadowSettings csm {};
    if (!WithDirectional(
          state, [&csm](auto& light) { csm = light.CascadedShadows(); })) {
      lua_pushnil(state);
      return 1;
    }

    lua_createtable(state, 0, 7);
    lua_pushinteger(state, static_cast<lua_Integer>(csm.cascade_count));
    lua_setfield(state, -2, "cascade_count");
    lua_pushinteger(state, static_cast<lua_Integer>(csm.split_mode));
    lua_setfield(state, -2, "split_mode");
    lua_pushnumber(state, csm.max_shadow_distance);
    lua_setfield(state, -2, "max_shadow_distance");
    lua_pushnumber(state, csm.distribution_exponent);
    lua_setfield(state, -2, "distribution_exponent");
    lua_pushnumber(state, csm.transition_fraction);
    lua_setfield(state, -2, "transition_fraction");
    lua_pushnumber(state, csm.distance_fadeout_fraction);
    lua_setfield(state, -2, "distance_fadeout_fraction");
    lua_createtable(state, static_cast<int>(scene::kMaxShadowCascades), 0);
    for (std::uint32_t i = 0; i < scene::kMaxShadowCascades; ++i) {
      lua_pushnumber(state, csm.cascade_distances.at(i));
      lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    lua_setfield(state, -2, "cascade_distances");
    return 1;
  }

  auto SceneNodeLightSetCascadedShadows(lua_State* state) -> int
  {
    const bool ok = lua_type(state, 2) == LUA_TTABLE
      && PatchTyped<scene::DirectionalLight>(state, [state](auto& light) {
           return ReadCsm(state, 2, light.CascadedShadows());
         });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  template <typename Fn>
  auto GetDirectionalFloat(lua_State* state, Fn&& fn) -> int
  {
    float v = 0.0F;
    if (!WithDirectional(
          state, [&v, &fn](auto& l) -> auto { v = std::forward<Fn>(fn)(l); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, v);
    return 1;
  }

  template <typename Fn>
  auto SetDirectionalFloat(lua_State* state, Fn&& fn) -> int
  {
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto v = static_cast<float>(lua_tonumber(state, 2));
    lua_pushboolean(state,
      EditDirectional(
        state, [v, &fn](auto& l) -> auto { std::forward<Fn>(fn)(l, v); })
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeLightGetIntensityLux(lua_State* state) -> int
  {
    return GetDirectionalFloat(
      state, [](auto& l) { return l.GetIntensityLux(); });
  }

  auto SceneNodeLightSetIntensityLux(lua_State* state) -> int
  {
    return SetDirectionalFloat(
      state, [](auto& l, const float v) { l.SetIntensityLux(v); });
  }

  auto SceneNodeLightGetAngularSizeRadians(lua_State* state) -> int
  {
    return GetDirectionalFloat(
      state, [](auto& l) { return l.GetAngularSizeRadians(); });
  }

  auto SceneNodeLightSetAngularSizeRadians(lua_State* state) -> int
  {
    return SetDirectionalFloat(
      state, [](auto& l, const float v) { l.SetAngularSizeRadians(v); });
  }

  template <typename Fn>
  auto GetPointOrSpotFloat(lua_State* state, Fn&& fn) -> int
  {
    float v = 0.0F;
    if (WithPoint(state, [&v, &fn](auto& l) { v = std::forward<Fn>(fn)(l); })
      || WithSpot(state, [&v, &fn](auto& l) { v = std::forward<Fn>(fn)(l); })) {
      lua_pushnumber(state, v);
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  template <typename Fn>
  auto SetPointOrSpotFloat(lua_State* state, Fn&& fn) -> int
  {
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto v = static_cast<float>(lua_tonumber(state, 2));
    const bool ok
      = EditPoint(state, [v, &fn](auto& l) { std::forward<Fn>(fn)(l, v); })
      || EditSpot(state, [v, &fn](auto& l) { std::forward<Fn>(fn)(l, v); });
    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightGetRange(lua_State* state) -> int
  {
    return GetPointOrSpotFloat(state, [](auto& l) { return l.GetRange(); });
  }

  auto SceneNodeLightSetRange(lua_State* state) -> int
  {
    return SetPointOrSpotFloat(
      state, [](auto& l, const float v) { l.SetRange(v); });
  }

  auto SceneNodeLightGetSourceRadius(lua_State* state) -> int
  {
    return GetPointOrSpotFloat(
      state, [](auto& l) { return l.GetSourceRadius(); });
  }

  auto SceneNodeLightSetSourceRadius(lua_State* state) -> int
  {
    return SetPointOrSpotFloat(
      state, [](auto& l, const float v) { l.SetSourceRadius(v); });
  }

  auto SceneNodeLightGetLuminousFluxLm(lua_State* state) -> int
  {
    return GetPointOrSpotFloat(
      state, [](auto& l) { return l.GetLuminousFluxLm(); });
  }

  auto SceneNodeLightSetLuminousFluxLm(lua_State* state) -> int
  {
    return SetPointOrSpotFloat(
      state, [](auto& l, const float v) { l.SetLuminousFluxLm(v); });
  }

  auto SceneNodeLightGetInnerConeAngleRadians(lua_State* state) -> int
  {
    float v = 0.0F;
    if (!WithSpot(state, [&v](auto& l) { v = l.GetInnerConeAngleRadians(); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, v);
    return 1;
  }

  auto SceneNodeLightSetInnerConeAngleRadians(lua_State* state) -> int
  {
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto v = static_cast<float>(lua_tonumber(state, 2));
    lua_pushboolean(state,
      EditSpot(state, [v](auto& l) { l.SetInnerConeAngleRadians(v); }) ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightGetOuterConeAngleRadians(lua_State* state) -> int
  {
    float v = 0.0F;
    if (!WithSpot(state, [&v](auto& l) { v = l.GetOuterConeAngleRadians(); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, v);
    return 1;
  }

  auto SceneNodeLightSetOuterConeAngleRadians(lua_State* state) -> int
  {
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto v = static_cast<float>(lua_tonumber(state, 2));
    lua_pushboolean(state,
      EditSpot(state, [v](auto& l) { l.SetOuterConeAngleRadians(v); }) ? 1 : 0);
    return 1;
  }
  auto SceneNodeLightGetAtmosphereLightSlot(lua_State* state) -> int
  {
    scene::AtmosphereLightSlot slot {};
    if (!WithDirectional(state, [&slot](const auto& light) {
          slot = light.GetAtmosphereLightSlot();
        })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushstring(state, AtmosphereLightSlotName(slot));
    return 1;
  }

  auto SceneNodeLightSetAtmosphereLightSlot(lua_State* state) -> int
  {
    if (lua_type(state, 2) != LUA_TSTRING) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto name = std::string_view(lua_tostring(state, 2));
    const auto slot = TryParseAtmosphereLightSlot(name);
    const bool ok = slot.has_value()
      && EditDirectional(
        state, [slot](auto& light) { light.SetAtmosphereLightSlot(*slot); });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  auto SceneNodeLightGetPerPixelTransmittance(lua_State* state) -> int
  {
    bool enabled = false;
    if (!WithDirectional(state, [&enabled](const auto& light) {
          enabled = light.GetUsePerPixelAtmosphereTransmittance();
        })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushboolean(state, static_cast<int>(enabled));
    return 1;
  }

  auto SceneNodeLightSetPerPixelTransmittance(lua_State* state) -> int
  {
    const bool ok = lua_type(state, 2) == LUA_TBOOLEAN
      && EditDirectional(state, [state](auto& light) {
           light.SetUsePerPixelAtmosphereTransmittance(
             lua_toboolean(state, 2) != 0);
         });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  auto SceneNodeLightGetDiskScale(lua_State* state) -> int
  {
    Vec3 value;
    if (!WithDirectional(state, [&value](const auto& light) {
          value = light.GetAtmosphereDiskLuminanceScale();
        })) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, value);
  }

  auto SceneNodeLightSetDiskScale(lua_State* state) -> int
  {
    Vec3 value;
    const bool ok = TryCheckVec3(state, 2, value)
      && EditDirectional(state,
        [value](auto& light) { light.SetAtmosphereDiskLuminanceScale(value); });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  auto SceneNodeLightSetConeAngles(lua_State* state) -> int
  {
    const bool ok = lua_type(state, 2) == LUA_TNUMBER
      && lua_type(state, 3) == LUA_TNUMBER
      && EditSpot(state, [state](auto& light) {
           light.SetConeAnglesRadians(
             static_cast<float>(lua_tonumber(state, 2)),
             static_cast<float>(lua_tonumber(state, 3)));
         });
    lua_pushboolean(state, static_cast<int>(ok));
    return 1;
  }

  constexpr auto kLightMethods = std::to_array<BindingFunction>({
    {
      .name = "light",
      .function = SceneNodeLight,
      .signature = "() -> SceneNode?",
    },
    {
      .name = "light_update",
      .function = SceneNodeLightUpdate,
      .signature = "(properties: LightProperties) -> boolean",
    },
    {
      .name = "light_get_atmosphere_light_slot",
      .function = SceneNodeLightGetAtmosphereLightSlot,
      .signature = "() -> AtmosphereLightSlot?",
    },
    {
      .name = "light_set_atmosphere_light_slot",
      .function = SceneNodeLightSetAtmosphereLightSlot,
      .signature = "(slot: AtmosphereLightSlot) -> boolean",
    },
    {
      .name = "light_get_use_per_pixel_atmosphere_transmittance",
      .function = SceneNodeLightGetPerPixelTransmittance,
      .signature = "() -> boolean?",
    },
    {
      .name = "light_set_use_per_pixel_atmosphere_transmittance",
      .function = SceneNodeLightSetPerPixelTransmittance,
      .signature = "(enabled: boolean) -> boolean",
    },
    {
      .name = "light_get_atmosphere_disk_luminance_scale_rgb",
      .function = SceneNodeLightGetDiskScale,
      .signature = "() -> vector?",
    },
    {
      .name = "light_set_atmosphere_disk_luminance_scale_rgb",
      .function = SceneNodeLightSetDiskScale,
      .signature = "(scale: vector) -> boolean",
    },
    {
      .name = "light_set_cone_angles_radians",
      .function = SceneNodeLightSetConeAngles,
      .signature = "(inner: number, outer: number) -> boolean",
    },
    {
      .name = "attach_directional_light",
      .function = SceneNodeAttachDirectionalLight,
      .signature = "(properties: LightProperties?) -> boolean",
    },
    {
      .name = "attach_point_light",
      .function = SceneNodeAttachPointLight,
      .signature = "(properties: LightProperties?) -> boolean",
    },
    {
      .name = "attach_spot_light",
      .function = SceneNodeAttachSpotLight,
      .signature = "(properties: LightProperties?) -> boolean",
    },
    {
      .name = "detach_light",
      .function = SceneNodeDetachLight,
      .signature = "() -> boolean",
    },
    {
      .name = "has_light",
      .function = SceneNodeHasLight,
      .signature = "() -> boolean",
    },
    {
      .name = "light_type",
      .function = SceneNodeLightType,
      .signature = "() -> LightType?",
    },
    {
      .name = "light_get_affects_world",
      .function = SceneNodeLightGetAffectsWorld,
      .signature = "() -> boolean?",
    },
    {
      .name = "light_set_affects_world",
      .function = SceneNodeLightSetAffectsWorld,
      .signature = "(affects: boolean) -> boolean",
    },
    {
      .name = "light_get_color_rgb",
      .function = SceneNodeLightGetColorRgb,
      .signature = "() -> vector?",
    },
    {
      .name = "light_set_color_rgb",
      .function = SceneNodeLightSetColorRgb,
      .signature = "(color: vector) -> boolean",
    },
    {
      .name = "light_get_mobility",
      .function = SceneNodeLightGetMobility,
      .signature = "() -> LightMobility?",
    },
    {
      .name = "light_set_mobility",
      .function = SceneNodeLightSetMobility,
      .signature = "(mobility: LightMobility) -> boolean",
    },
    {
      .name = "light_get_casts_shadows",
      .function = SceneNodeLightGetCastsShadows,
      .signature = "() -> boolean?",
    },
    {
      .name = "light_set_casts_shadows",
      .function = SceneNodeLightSetCastsShadows,
      .signature = "(casts: boolean) -> boolean",
    },
    {
      .name = "light_get_exposure_compensation_ev",
      .function = SceneNodeLightGetExposureCompensationEv,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_exposure_compensation_ev",
      .function = SceneNodeLightSetExposureCompensationEv,
      .signature = "(ev: number) -> boolean",
    },
    {
      .name = "light_get_shadow_settings",
      .function = SceneNodeLightGetShadowSettings,
      .signature = "() -> ShadowSettings?",
    },
    {
      .name = "light_set_shadow_settings",
      .function = SceneNodeLightSetShadowSettings,
      .signature = "(settings: ShadowSettingsInput) -> boolean",
    },
    {
      .name = "light_get_intensity_lux",
      .function = SceneNodeLightGetIntensityLux,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_intensity_lux",
      .function = SceneNodeLightSetIntensityLux,
      .signature = "(lux: number) -> boolean",
    },
    {
      .name = "light_get_angular_size_radians",
      .function = SceneNodeLightGetAngularSizeRadians,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_angular_size_radians",
      .function = SceneNodeLightSetAngularSizeRadians,
      .signature = "(radians: number) -> boolean",
    },
    {
      .name = "light_get_cascaded_shadows",
      .function = SceneNodeLightGetCascadedShadows,
      .signature = "() -> CascadedShadowSettings?",
    },
    {
      .name = "light_set_cascaded_shadows",
      .function = SceneNodeLightSetCascadedShadows,
      .signature = "(settings: CascadedShadowSettingsInput) -> boolean",
    },
    {
      .name = "light_get_range",
      .function = SceneNodeLightGetRange,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_range",
      .function = SceneNodeLightSetRange,
      .signature = "(range: number) -> boolean",
    },
    {
      .name = "light_get_source_radius",
      .function = SceneNodeLightGetSourceRadius,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_source_radius",
      .function = SceneNodeLightSetSourceRadius,
      .signature = "(radius: number) -> boolean",
    },
    {
      .name = "light_get_luminous_flux_lm",
      .function = SceneNodeLightGetLuminousFluxLm,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_luminous_flux_lm",
      .function = SceneNodeLightSetLuminousFluxLm,
      .signature = "(lumens: number) -> boolean",
    },
    {
      .name = "light_get_inner_cone_angle_radians",
      .function = SceneNodeLightGetInnerConeAngleRadians,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_inner_cone_angle_radians",
      .function = SceneNodeLightSetInnerConeAngleRadians,
      .signature = "(radians: number) -> boolean",
    },
    {
      .name = "light_get_outer_cone_angle_radians",
      .function = SceneNodeLightGetOuterConeAngleRadians,
      .signature = "() -> number?",
    },
    {
      .name = "light_set_outer_cone_angle_radians",
      .function = SceneNodeLightSetOuterConeAngleRadians,
      .signature = "(radians: number) -> boolean",
    },
  });

  constexpr auto kLightClasses = std::to_array<BindingClass>({
    {
      .name = "SceneNode",
      .metatable = kSceneNodeMetatable,
      .methods = kLightMethods,
    },
  });

  constexpr auto kLightTypes = std::to_array<BindingTypeAlias>({
    {
      .name = "LightType",
      .definition = R"("directional" | "point" | "spot")",
    },
    {
      .name = "LightMobility",
      .definition = R"("realtime" | "mixed" | "baked")",
    },
    {
      .name = "ShadowResolutionHint",
      .definition = R"("low" | "medium" | "high" | "ultra")",
    },
    {
      .name = "AtmosphereLightSlot",
      .definition = R"("none" | "primary" | "secondary")",
    },
    {
      .name = "ShadowSettings",
      .definition = "{ bias: number, normal_bias: number, "
                    "contact_shadows: boolean, "
                    "resolution_hint: ShadowResolutionHint }",
    },
    {
      .name = "ShadowSettingsInput",
      .definition = "{ bias: number?, normal_bias: number?, "
                    "contact_shadows: boolean?, "
                    "resolution_hint: ShadowResolutionHint? }",
    },
    {
      .name = "CascadedShadowSettings",
      .definition = "{ cascade_count: number, split_mode: number, "
                    "max_shadow_distance: number, "
                    "distribution_exponent: number, "
                    "transition_fraction: number, "
                    "distance_fadeout_fraction: number, "
                    "cascade_distances: { number } }",
    },
    {
      .name = "CascadedShadowSettingsInput",
      .definition = "{ cascade_count: number?, split_mode: number?, "
                    "max_shadow_distance: number?, "
                    "distribution_exponent: number?, "
                    "transition_fraction: number?, "
                    "distance_fadeout_fraction: number?, "
                    "cascade_distances: { number }? }",
    },
    {
      .name = "LightProperties",
      .definition = "{ affects_world: boolean?, color_rgb: vector?, "
                    "mobility: LightMobility?, casts_shadows: boolean?, "
                    "exposure_compensation_ev: number?, "
                    "shadow: ShadowSettingsInput?, intensity_lux: number?, "
                    "angular_size_radians: number?, "
                    "use_per_pixel_atmosphere_transmittance: boolean?, "
                    "atmosphere_disk_luminance_scale_rgb: vector?, "
                    "atmosphere_light_slot: AtmosphereLightSlot?, "
                    "cascade_count: number?, split_mode: number?, "
                    "max_shadow_distance: number?, "
                    "distribution_exponent: number?, "
                    "transition_fraction: number?, "
                    "distance_fadeout_fraction: number?, "
                    "cascade_distances: { number }?, range: number?, "
                    "source_radius: number?, luminous_flux_lm: number?, "
                    "inner_cone_angle_radians: number?, "
                    "outer_cone_angle_radians: number? }",
    },
  });

  constexpr auto kLightDescription
    = std::to_array<BindingNamespaceDescription>({
      { .path = "scene", .classes = kLightClasses, .types = kLightTypes },
    });
} // namespace

auto RegisterSceneNodeLightMethods(lua_State* state, const int metatable_index)
  -> void
{
  RegisterFunctions(state, metatable_index, kLightMethods);
}

auto DescribeSceneNodeLightBindings()
  -> std::span<const BindingNamespaceDescription>
{
  return kLightDescription;
}

} // namespace oxygen::scripting::bindings
