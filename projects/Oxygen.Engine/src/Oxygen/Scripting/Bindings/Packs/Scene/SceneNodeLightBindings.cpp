//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
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

  auto AttenuationModelToString(const scene::AttenuationModel attenuation)
    -> const char*
  {
    switch (attenuation) {
    case scene::AttenuationModel::kInverseSquare:
      return "inverse_square";
    case scene::AttenuationModel::kLinear:
      return "linear";
    case scene::AttenuationModel::kCustomExponent:
      return "custom_exponent";
    }
    return "inverse_square";
  }

  auto TryParseAttenuationModel(
    const std::string_view value, scene::AttenuationModel& out) -> bool
  {
    if (value == "inverse_square") {
      out = scene::AttenuationModel::kInverseSquare;
      return true;
    }
    if (value == "linear") {
      out = scene::AttenuationModel::kLinear;
      return true;
    }
    if (value == "custom_exponent") {
      out = scene::AttenuationModel::kCustomExponent;
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
    auto* node = CheckSceneNode(state, 1);
    auto light = node->GetLightAs<scene::DirectionalLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithPoint(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = CheckSceneNode(state, 1);
    auto light = node->GetLightAs<scene::PointLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithSpot(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = CheckSceneNode(state, 1);
    auto light = node->GetLightAs<scene::SpotLight>();
    if (!light.has_value()) {
      return false;
    }
    std::forward<Fn>(fn)(light->get());
    return true;
  }

  template <typename Fn> auto WithAny(lua_State* state, Fn&& fn) -> bool
  {
    auto* node = CheckSceneNode(state, 1);

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

  auto SceneNodeLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    if (!node->HasLight()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeAttachDirectionalLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    auto light = std::make_unique<scene::DirectionalLight>();
    if (lua_istable(state, 2) != 0) {
      ApplyCommon(state, 2, light->Common());
      float n = 0.0F;
      bool b = false;
      if (TryGetNumberField(state, 2, "intensity_lux", n)) {
        light->SetIntensityLux(n);
      }
      if (TryGetNumberField(state, 2, "angular_size_radians", n)) {
        light->SetAngularSizeRadians(n);
      }
      if (TryGetBoolField(state, 2, "environment_contribution", b)) {
        light->SetEnvironmentContribution(b);
      }
      if (TryGetBoolField(state, 2, "is_sun_light", b)) {
        light->SetIsSunLight(b);
      }
    }
    lua_pushboolean(state, node->AttachLight(std::move(light)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeAttachPointLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    auto light = std::make_unique<scene::PointLight>();
    if (lua_istable(state, 2) != 0) {
      ApplyCommon(state, 2, light->Common());
      float n = 0.0F;
      if (TryGetNumberField(state, 2, "range", n)) {
        light->SetRange(n);
      }
      if (TryGetNumberField(state, 2, "decay_exponent", n)) {
        light->SetDecayExponent(n);
      }
      if (TryGetNumberField(state, 2, "source_radius", n)) {
        light->SetSourceRadius(n);
      }
      if (TryGetNumberField(state, 2, "luminous_flux_lm", n)) {
        light->SetLuminousFluxLm(n);
      }
    }
    lua_pushboolean(state, node->AttachLight(std::move(light)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeAttachSpotLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    auto light = std::make_unique<scene::SpotLight>();
    if (lua_istable(state, 2) != 0) {
      ApplyCommon(state, 2, light->Common());
      float n = 0.0F;
      if (TryGetNumberField(state, 2, "range", n)) {
        light->SetRange(n);
      }
      if (TryGetNumberField(state, 2, "decay_exponent", n)) {
        light->SetDecayExponent(n);
      }
      if (TryGetNumberField(state, 2, "source_radius", n)) {
        light->SetSourceRadius(n);
      }
      if (TryGetNumberField(state, 2, "luminous_flux_lm", n)) {
        light->SetLuminousFluxLm(n);
      }
      if (TryGetNumberField(state, 2, "inner_cone_angle_radians", n)) {
        light->SetInnerConeAngleRadians(n);
      }
      if (TryGetNumberField(state, 2, "outer_cone_angle_radians", n)) {
        light->SetOuterConeAngleRadians(n);
      }
    }
    lua_pushboolean(state, node->AttachLight(std::move(light)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeDetachLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    lua_pushboolean(state, node->DetachLight() ? 1 : 0);
    return 1;
  }

  auto SceneNodeHasLight(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
    lua_pushboolean(state, node->HasLight() ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightType(lua_State* state) -> int
  {
    auto* node = CheckSceneNode(state, 1);
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
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      WithAny(state, [v](auto& l) { l.Common().affects_world = v; }) ? 1 : 0);
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
    const auto v = CheckVec3(state, 2, "light_set_color_rgb expects vector");
    lua_pushboolean(state,
      WithAny(state, [v](auto& l) { l.Common().color_rgb = v; }) ? 1 : 0);
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
    const char* text = luaL_checklstring(state, 2, &len);
    scene::LightMobility value {};
    if (!TryParseMobility(std::string_view(text, len), value)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state,
      WithAny(state, [value](auto& l) { l.Common().mobility = value; }) ? 1
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
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      WithAny(state, [v](auto& l) { l.Common().casts_shadows = v; }) ? 1 : 0);
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
    const float v = static_cast<float>(luaL_checknumber(state, 2));
    lua_pushboolean(state,
      WithAny(state, [v](auto& l) { l.Common().exposure_compensation_ev = v; })
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
    luaL_checktype(state, 2, LUA_TTABLE);
    bool ok = false;
    if (!WithAny(state, [state, &ok](auto& light) {
          auto settings = light.Common().shadow;
          float number = 0.0F;
          bool boolean = false;
          bool changed = false;

          if (TryGetNumberField(state, 2, "bias", number)) {
            settings.bias = number;
            changed = true;
          }
          if (TryGetNumberField(state, 2, "normal_bias", number)) {
            settings.normal_bias = number;
            changed = true;
          }
          if (TryGetBoolField(state, 2, "contact_shadows", boolean)) {
            settings.contact_shadows = boolean;
            changed = true;
          }
          lua_getfield(state, 2, "resolution_hint");
          size_t len = 0;
          const char* text = lua_tolstring(state, -1, &len);
          if (text != nullptr) {
            scene::ShadowResolutionHint hint = settings.resolution_hint;
            if (TryParseShadowResolutionHint(
                  std::string_view(text, len), hint)) {
              settings.resolution_hint = hint;
              changed = true;
            } else {
              lua_pop(state, 1);
              ok = false;
              return;
            }
          }
          lua_pop(state, 1);

          if (changed) {
            light.Common().shadow = settings;
          }
          ok = changed;
        })) {
      lua_pushboolean(state, 0);
      return 1;
    }

    lua_pushboolean(state, ok ? 1 : 0);
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

    lua_createtable(state, 0, 3);
    lua_pushinteger(state, static_cast<lua_Integer>(csm.cascade_count));
    lua_setfield(state, -2, "cascade_count");
    lua_pushnumber(state, csm.distribution_exponent);
    lua_setfield(state, -2, "distribution_exponent");
    lua_createtable(state, static_cast<int>(csm.cascade_count), 0);
    for (std::uint32_t i = 0; i < csm.cascade_count; ++i) {
      lua_pushnumber(state, csm.cascade_distances.at(i));
      lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    lua_setfield(state, -2, "cascade_distances");
    return 1;
  }

  auto SceneNodeLightSetCascadedShadows(lua_State* state) -> int
  {
    luaL_checktype(state, 2, LUA_TTABLE);
    bool ok = false;
    if (!WithDirectional(state, [state, &ok](auto& light) {
          auto csm = light.CascadedShadows();
          bool changed = false;

          lua_getfield(state, 2, "cascade_count");
          if (lua_isnumber(state, -1) != 0) {
            auto count = static_cast<std::uint32_t>(lua_tointeger(state, -1));
            count = (std::max)(count, 1U);
            count = (std::min)(count, scene::kMaxShadowCascades);
            csm.cascade_count = count;
            changed = true;
          }
          lua_pop(state, 1);

          float number = 0.0F;
          if (TryGetNumberField(state, 2, "distribution_exponent", number)) {
            csm.distribution_exponent = number;
            changed = true;
          }

          lua_getfield(state, 2, "cascade_distances");
          if (lua_istable(state, -1) != 0) {
            for (std::uint32_t i = 0; i < csm.cascade_count; ++i) {
              lua_rawgeti(state, -1, static_cast<int>(i + 1));
              if (lua_isnumber(state, -1) != 0) {
                csm.cascade_distances.at(i)
                  = static_cast<float>(lua_tonumber(state, -1));
              }
              lua_pop(state, 1);
            }
            changed = true;
          }
          lua_pop(state, 1);

          if (changed) {
            light.CascadedShadows() = csm;
          }
          ok = changed;
        })) {
      lua_pushboolean(state, 0);
      return 1;
    }

    lua_pushboolean(state, ok ? 1 : 0);
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
    const auto v = static_cast<float>(luaL_checknumber(state, 2));
    lua_pushboolean(state,
      WithDirectional(
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

  auto SceneNodeLightGetEnvironmentContribution(lua_State* state) -> int
  {
    bool v = false;
    if (!WithDirectional(
          state, [&v](auto& l) { v = l.GetEnvironmentContribution(); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushboolean(state, v ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightSetEnvironmentContribution(lua_State* state) -> int
  {
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      WithDirectional(state, [v](auto& l) { l.SetEnvironmentContribution(v); })
        ? 1
        : 0);
    return 1;
  }

  auto SceneNodeLightGetIsSunLight(lua_State* state) -> int
  {
    bool v = false;
    if (!WithDirectional(state, [&v](auto& l) { v = l.IsSunLight(); })) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushboolean(state, v ? 1 : 0);
    return 1;
  }

  auto SceneNodeLightSetIsSunLight(lua_State* state) -> int
  {
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    const bool v = lua_toboolean(state, 2) != 0;
    lua_pushboolean(state,
      WithDirectional(state, [v](auto& l) { l.SetIsSunLight(v); }) ? 1 : 0);
    return 1;
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
    const float v = static_cast<float>(luaL_checknumber(state, 2));
    const bool ok
      = WithPoint(state, [v, &fn](auto& l) { std::forward<Fn>(fn)(l, v); })
      || WithSpot(state, [v, &fn](auto& l) { std::forward<Fn>(fn)(l, v); });
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

  auto SceneNodeLightGetDecayExponent(lua_State* state) -> int
  {
    return GetPointOrSpotFloat(
      state, [](auto& l) { return l.GetDecayExponent(); });
  }

  auto SceneNodeLightSetDecayExponent(lua_State* state) -> int
  {
    return SetPointOrSpotFloat(
      state, [](auto& l, const float v) { l.SetDecayExponent(v); });
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

  auto SceneNodeLightGetAttenuationModel(lua_State* state) -> int
  {
    scene::AttenuationModel v {};
    if (WithPoint(state, [&v](auto& l) { v = l.GetAttenuationModel(); })
      || WithSpot(state, [&v](auto& l) { v = l.GetAttenuationModel(); })) {
      lua_pushstring(state, AttenuationModelToString(v));
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeLightSetAttenuationModel(lua_State* state) -> int
  {
    size_t len = 0;
    const char* text = luaL_checklstring(state, 2, &len);
    scene::AttenuationModel value {};
    if (!TryParseAttenuationModel(std::string_view(text, len), value)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const bool ok
      = WithPoint(state, [value](auto& l) { l.SetAttenuationModel(value); })
      || WithSpot(state, [value](auto& l) { l.SetAttenuationModel(value); });
    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
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
    const float v = static_cast<float>(luaL_checknumber(state, 2));
    lua_pushboolean(state,
      WithSpot(state, [v](auto& l) { l.SetInnerConeAngleRadians(v); }) ? 1 : 0);
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
    const float v = static_cast<float>(luaL_checknumber(state, 2));
    lua_pushboolean(state,
      WithSpot(state, [v](auto& l) { l.SetOuterConeAngleRadians(v); }) ? 1 : 0);
    return 1;
  }
} // namespace

auto RegisterSceneNodeLightMethods(lua_State* state, const int metatable_index)
  -> void
{
  constexpr auto methods = std::to_array<luaL_Reg>({
    { .name = "light", .func = SceneNodeLight },
    { .name = "attach_directional_light",
      .func = SceneNodeAttachDirectionalLight },
    { .name = "attach_point_light", .func = SceneNodeAttachPointLight },
    { .name = "attach_spot_light", .func = SceneNodeAttachSpotLight },
    { .name = "detach_light", .func = SceneNodeDetachLight },
    { .name = "has_light", .func = SceneNodeHasLight },
    { .name = "light_type", .func = SceneNodeLightType },
    { .name = "light_get_affects_world",
      .func = SceneNodeLightGetAffectsWorld },
    { .name = "light_set_affects_world",
      .func = SceneNodeLightSetAffectsWorld },
    { .name = "light_get_color_rgb", .func = SceneNodeLightGetColorRgb },
    { .name = "light_set_color_rgb", .func = SceneNodeLightSetColorRgb },
    { .name = "light_get_mobility", .func = SceneNodeLightGetMobility },
    { .name = "light_set_mobility", .func = SceneNodeLightSetMobility },
    { .name = "light_get_casts_shadows",
      .func = SceneNodeLightGetCastsShadows },
    { .name = "light_set_casts_shadows",
      .func = SceneNodeLightSetCastsShadows },
    { .name = "light_get_exposure_compensation_ev",
      .func = SceneNodeLightGetExposureCompensationEv },
    { .name = "light_set_exposure_compensation_ev",
      .func = SceneNodeLightSetExposureCompensationEv },
    { .name = "light_get_shadow_settings",
      .func = SceneNodeLightGetShadowSettings },
    { .name = "light_set_shadow_settings",
      .func = SceneNodeLightSetShadowSettings },
    { .name = "light_get_intensity_lux",
      .func = SceneNodeLightGetIntensityLux },
    { .name = "light_set_intensity_lux",
      .func = SceneNodeLightSetIntensityLux },
    { .name = "light_get_angular_size_radians",
      .func = SceneNodeLightGetAngularSizeRadians },
    { .name = "light_set_angular_size_radians",
      .func = SceneNodeLightSetAngularSizeRadians },
    { .name = "light_get_environment_contribution",
      .func = SceneNodeLightGetEnvironmentContribution },
    { .name = "light_set_environment_contribution",
      .func = SceneNodeLightSetEnvironmentContribution },
    { .name = "light_get_is_sun_light", .func = SceneNodeLightGetIsSunLight },
    { .name = "light_set_is_sun_light", .func = SceneNodeLightSetIsSunLight },
    { .name = "light_get_cascaded_shadows",
      .func = SceneNodeLightGetCascadedShadows },
    { .name = "light_set_cascaded_shadows",
      .func = SceneNodeLightSetCascadedShadows },
    { .name = "light_get_range", .func = SceneNodeLightGetRange },
    { .name = "light_set_range", .func = SceneNodeLightSetRange },
    { .name = "light_get_attenuation_model",
      .func = SceneNodeLightGetAttenuationModel },
    { .name = "light_set_attenuation_model",
      .func = SceneNodeLightSetAttenuationModel },
    { .name = "light_get_decay_exponent",
      .func = SceneNodeLightGetDecayExponent },
    { .name = "light_set_decay_exponent",
      .func = SceneNodeLightSetDecayExponent },
    { .name = "light_get_source_radius",
      .func = SceneNodeLightGetSourceRadius },
    { .name = "light_set_source_radius",
      .func = SceneNodeLightSetSourceRadius },
    { .name = "light_get_luminous_flux_lm",
      .func = SceneNodeLightGetLuminousFluxLm },
    { .name = "light_set_luminous_flux_lm",
      .func = SceneNodeLightSetLuminousFluxLm },
    { .name = "light_get_inner_cone_angle_radians",
      .func = SceneNodeLightGetInnerConeAngleRadians },
    { .name = "light_set_inner_cone_angle_radians",
      .func = SceneNodeLightSetInnerConeAngleRadians },
    { .name = "light_get_outer_cone_angle_radians",
      .func = SceneNodeLightGetOuterConeAngleRadians },
    { .name = "light_set_outer_cone_angle_radians",
      .func = SceneNodeLightSetOuterConeAngleRadians },
  });

  for (const auto& reg : methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, metatable_index, reg.name);
  }
}

} // namespace oxygen::scripting::bindings
