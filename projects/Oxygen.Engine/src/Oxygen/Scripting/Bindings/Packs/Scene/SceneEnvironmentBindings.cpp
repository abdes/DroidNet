//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  auto HasMetatable(
    lua_State* state, const int index, const char* metatable_name) -> bool
  {
    if (lua_type(state, index) != LUA_TUSERDATA) {
      return false;
    }
    if (lua_getmetatable(state, index) == 0) {
      return false;
    }
    luaL_getmetatable(state, metatable_name);
    const bool matches = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    return matches;
  }

  constexpr const char* kEnvironmentMetatable = "oxygen.scene.environment";
  constexpr const char* kFogSystemMetatable = "oxygen.scene.environment.fog";
  constexpr const char* kSkyAtmosphereMetatable
    = "oxygen.scene.environment.sky_atmosphere";
  constexpr const char* kSkyLightMetatable
    = "oxygen.scene.environment.sky_light";
  constexpr const char* kSkySphereMetatable
    = "oxygen.scene.environment.sky_sphere";
  constexpr const char* kSunMetatable = "oxygen.scene.environment.sun";
  constexpr const char* kCloudsMetatable = "oxygen.scene.environment.clouds";
  constexpr const char* kPostProcessMetatable
    = "oxygen.scene.environment.post_process";

  struct EnvironmentUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };

  struct FogSystemUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct SkyAtmosphereUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct SkyLightUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct SkySphereUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct SunUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct CloudsUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };
  struct PostProcessUserdata {
    observer_ptr<scene::Scene> scene_ref;
  };

  auto CheckEnvironment(lua_State* state, const int index)
    -> EnvironmentUserdata*
  {
    static EnvironmentUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kEnvironmentMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<EnvironmentUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }

  auto CheckFogSystem(lua_State* state, const int index) -> FogSystemUserdata*
  {
    static FogSystemUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kFogSystemMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<FogSystemUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }

  auto ResolveEnvironment(const observer_ptr<scene::Scene> scene_ref)
    -> observer_ptr<scene::SceneEnvironment>
  {
    if (scene_ref == nullptr) {
      return nullptr;
    }
    return scene_ref->GetEnvironment();
  }

  auto ResolveFog(const observer_ptr<scene::Scene> scene_ref)
    -> observer_ptr<scene::environment::Fog>
  {
    const auto env = ResolveEnvironment(scene_ref);
    if (env == nullptr) {
      return nullptr;
    }
    return env->TryGetSystem<scene::environment::Fog>();
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

  auto TryGetIntegerField(lua_State* state, const int table_index,
    const char* key, std::uint64_t& out) -> bool
  {
    lua_getfield(state, table_index, key);
    const bool ok = lua_isnumber(state, -1) != 0;
    if (ok) {
      out = static_cast<std::uint64_t>(lua_tointeger(state, -1));
    }
    lua_pop(state, 1);
    return ok;
  }

  template <typename SystemT, typename UserdataT,
    UserdataT* (*Check)(lua_State*, int)>
  auto ResolveSystem(lua_State* state) -> observer_ptr<SystemT>
  {
    auto* ud = Check(state, 1);
    const auto env = ResolveEnvironment(ud->scene_ref);
    if (env == nullptr) {
      return nullptr;
    }
    return env->template TryGetSystem<SystemT>();
  }

  auto CheckSkyAtmosphere(lua_State* state, const int index)
    -> SkyAtmosphereUserdata*
  {
    static SkyAtmosphereUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kSkyAtmosphereMetatable)) {
      return &invalid;
    }
    auto* ud
      = static_cast<SkyAtmosphereUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }
  auto CheckSkyLight(lua_State* state, const int index) -> SkyLightUserdata*
  {
    static SkyLightUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kSkyLightMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<SkyLightUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }
  auto CheckSkySphere(lua_State* state, const int index) -> SkySphereUserdata*
  {
    static SkySphereUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kSkySphereMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<SkySphereUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }
  auto CheckSun(lua_State* state, const int index) -> SunUserdata*
  {
    static SunUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kSunMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<SunUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }
  auto CheckClouds(lua_State* state, const int index) -> CloudsUserdata*
  {
    static CloudsUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kCloudsMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<CloudsUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }
  auto CheckPostProcess(lua_State* state, const int index)
    -> PostProcessUserdata*
  {
    static PostProcessUserdata invalid { .scene_ref = nullptr };
    if (!HasMetatable(state, index, kPostProcessMetatable)) {
      return &invalid;
    }
    auto* ud = static_cast<PostProcessUserdata*>(lua_touserdata(state, index));
    return ud != nullptr ? ud : &invalid;
  }

  template <typename UserdataT>
  auto PushSystemUserdata(lua_State* state,
    observer_ptr<scene::Scene> scene_ref, const char* metatable_name) -> int
  {
    void* data = lua_newuserdata(state, sizeof(UserdataT));
    new (data) UserdataT { .scene_ref = scene_ref };
    luaL_getmetatable(state, metatable_name);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto EnvironmentSystems(lua_State* state) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    const auto env = ResolveEnvironment(env_ud->scene_ref);
    lua_newtable(state);
    if (env == nullptr) {
      return 1;
    }

    int idx = 1;
    if (env->HasSystem<scene::environment::Fog>()) {
      lua_pushliteral(state, "fog");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::SkyAtmosphere>()) {
      lua_pushliteral(state, "sky_atmosphere");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::SkyLight>()) {
      lua_pushliteral(state, "sky_light");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::SkySphere>()) {
      lua_pushliteral(state, "sky_sphere");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::Sun>()) {
      lua_pushliteral(state, "sun");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::VolumetricClouds>()) {
      lua_pushliteral(state, "clouds");
      lua_rawseti(state, -2, idx++);
    }
    if (env->HasSystem<scene::environment::PostProcessVolume>()) {
      lua_pushliteral(state, "post_process");
      lua_rawseti(state, -2, idx++);
    }
    return 1;
  }

  auto EnvironmentHasSystem(lua_State* state) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    size_t len = 0;
    const char* name_ptr = lua_tolstring(state, 2, &len);
    if (name_ptr == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const std::string_view name(name_ptr, len);
    const auto env = ResolveEnvironment(env_ud->scene_ref);
    if (env == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }

    if (name == "fog") {
      lua_pushboolean(state, env->HasSystem<scene::environment::Fog>() ? 1 : 0);
      return 1;
    }
    if (name == "sky_atmosphere") {
      lua_pushboolean(
        state, env->HasSystem<scene::environment::SkyAtmosphere>() ? 1 : 0);
      return 1;
    }
    if (name == "sky_light") {
      lua_pushboolean(
        state, env->HasSystem<scene::environment::SkyLight>() ? 1 : 0);
      return 1;
    }
    if (name == "sky_sphere") {
      lua_pushboolean(
        state, env->HasSystem<scene::environment::SkySphere>() ? 1 : 0);
      return 1;
    }
    if (name == "sun") {
      lua_pushboolean(state, env->HasSystem<scene::environment::Sun>() ? 1 : 0);
      return 1;
    }
    if (name == "clouds") {
      lua_pushboolean(
        state, env->HasSystem<scene::environment::VolumetricClouds>() ? 1 : 0);
      return 1;
    }
    if (name == "post_process") {
      lua_pushboolean(
        state, env->HasSystem<scene::environment::PostProcessVolume>() ? 1 : 0);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  auto EnvironmentRemoveSystem(lua_State* state) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    size_t len = 0;
    const char* name_ptr = lua_tolstring(state, 2, &len);
    if (name_ptr == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const std::string_view name(name_ptr, len);
    const auto env = ResolveEnvironment(env_ud->scene_ref);
    if (env == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }

    if (name == "fog") {
      const bool had = env->HasSystem<scene::environment::Fog>();
      if (had) {
        env->RemoveSystem<scene::environment::Fog>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "sky_atmosphere") {
      const bool had = env->HasSystem<scene::environment::SkyAtmosphere>();
      if (had) {
        env->RemoveSystem<scene::environment::SkyAtmosphere>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "sky_light") {
      const bool had = env->HasSystem<scene::environment::SkyLight>();
      if (had) {
        env->RemoveSystem<scene::environment::SkyLight>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "sky_sphere") {
      const bool had = env->HasSystem<scene::environment::SkySphere>();
      if (had) {
        env->RemoveSystem<scene::environment::SkySphere>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "sun") {
      const bool had = env->HasSystem<scene::environment::Sun>();
      if (had) {
        env->RemoveSystem<scene::environment::Sun>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "clouds") {
      const bool had = env->HasSystem<scene::environment::VolumetricClouds>();
      if (had) {
        env->RemoveSystem<scene::environment::VolumetricClouds>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    if (name == "post_process") {
      const bool had = env->HasSystem<scene::environment::PostProcessVolume>();
      if (had) {
        env->RemoveSystem<scene::environment::PostProcessVolume>();
      }
      lua_pushboolean(state, had ? 1 : 0);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  auto EnvironmentEnsureFog(lua_State* state) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    auto env = ResolveEnvironment(env_ud->scene_ref);
    if (env == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!env->HasSystem<scene::environment::Fog>()) {
      (void)env->AddSystem<scene::environment::Fog>();
    }
    return PushSystemUserdata<FogSystemUserdata>(
      state, env_ud->scene_ref, kFogSystemMetatable);
  }

  auto EnvironmentGetFog(lua_State* state) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    const auto fog = ResolveFog(env_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushSystemUserdata<FogSystemUserdata>(
      state, env_ud->scene_ref, kFogSystemMetatable);
  }

  template <typename T, typename UserdataT>
  auto EnsureSystem(lua_State* state, const char* metatable) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    auto env = ResolveEnvironment(env_ud->scene_ref);
    if (env == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!env->HasSystem<T>()) {
      (void)env->AddSystem<T>();
    }
    return PushSystemUserdata<UserdataT>(state, env_ud->scene_ref, metatable);
  }

  template <typename T, typename UserdataT>
  auto GetSystem(lua_State* state, const char* metatable) -> int
  {
    auto* env_ud = CheckEnvironment(state, 1);
    const auto env = ResolveEnvironment(env_ud->scene_ref);
    if ((env == nullptr) || !env->HasSystem<T>()) {
      lua_pushnil(state);
      return 1;
    }
    return PushSystemUserdata<UserdataT>(state, env_ud->scene_ref, metatable);
  }

  auto EnvironmentEnsureSkyAtmosphere(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::SkyAtmosphere,
      SkyAtmosphereUserdata>(state, kSkyAtmosphereMetatable);
  }
  auto EnvironmentGetSkyAtmosphere(lua_State* state) -> int
  {
    return GetSystem<scene::environment::SkyAtmosphere, SkyAtmosphereUserdata>(
      state, kSkyAtmosphereMetatable);
  }
  auto EnvironmentEnsureSkyLight(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::SkyLight, SkyLightUserdata>(
      state, kSkyLightMetatable);
  }
  auto EnvironmentGetSkyLight(lua_State* state) -> int
  {
    return GetSystem<scene::environment::SkyLight, SkyLightUserdata>(
      state, kSkyLightMetatable);
  }
  auto EnvironmentEnsureSkySphere(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::SkySphere, SkySphereUserdata>(
      state, kSkySphereMetatable);
  }
  auto EnvironmentGetSkySphere(lua_State* state) -> int
  {
    return GetSystem<scene::environment::SkySphere, SkySphereUserdata>(
      state, kSkySphereMetatable);
  }
  auto EnvironmentEnsureSun(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::Sun, SunUserdata>(
      state, kSunMetatable);
  }
  auto EnvironmentGetSun(lua_State* state) -> int
  {
    return GetSystem<scene::environment::Sun, SunUserdata>(
      state, kSunMetatable);
  }
  auto EnvironmentEnsureClouds(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::VolumetricClouds, CloudsUserdata>(
      state, kCloudsMetatable);
  }
  auto EnvironmentGetClouds(lua_State* state) -> int
  {
    return GetSystem<scene::environment::VolumetricClouds, CloudsUserdata>(
      state, kCloudsMetatable);
  }
  auto EnvironmentEnsurePostProcess(lua_State* state) -> int
  {
    return EnsureSystem<scene::environment::PostProcessVolume,
      PostProcessUserdata>(state, kPostProcessMetatable);
  }
  auto EnvironmentGetPostProcess(lua_State* state) -> int
  {
    return GetSystem<scene::environment::PostProcessVolume,
      PostProcessUserdata>(state, kPostProcessMetatable);
  }

  auto FogGetModel(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    switch (fog->GetModel()) {
    case scene::environment::FogModel::kExponentialHeight:
      lua_pushliteral(state, "exponential_height");
      break;
    case scene::environment::FogModel::kVolumetric:
      lua_pushliteral(state, "volumetric");
      break;
    default:
      lua_pushnil(state);
      break;
    }
    return 1;
  }

  auto FogSetModel(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    size_t len = 0;
    const char* v_ptr = lua_tolstring(state, 2, &len);
    if (v_ptr == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const std::string_view v(v_ptr, len);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (v == "exponential_height") {
      fog->SetModel(scene::environment::FogModel::kExponentialHeight);
      lua_pushboolean(state, 1);
      return 1;
    }
    if (v == "volumetric") {
      fog->SetModel(scene::environment::FogModel::kVolumetric);
      lua_pushboolean(state, 1);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  template <auto Getter> auto FogGetFloat(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, (fog.get()->*Getter)());
    return 1;
  }

  template <auto Setter> auto FogSetFloat(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const float v = static_cast<float>(lua_tonumber(state, 2));
    (fog.get()->*Setter)(v);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto FogGetSingleScatteringAlbedoRgb(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    return PushVec3(state, fog->GetSingleScatteringAlbedoRgb());
  }

  auto FogSetSingleScatteringAlbedoRgb(lua_State* state) -> int
  {
    auto* fog_ud = CheckFogSystem(state, 1);
    const auto fog = ResolveFog(fog_ud->scene_ref);
    if (fog == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    Vec3 value {};
    if (!TryCheckVec3(state, 2, value)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    fog->SetSingleScatteringAlbedoRgb(value);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SkyAtmosphereGet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkyAtmosphere,
      SkyAtmosphereUserdata, CheckSkyAtmosphere>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 14); // NOLINT(*-magic-numbers)
    lua_pushnumber(state, system->GetPlanetRadiusMeters());
    lua_setfield(state, -2, "planet_radius_meters");
    lua_pushnumber(state, system->GetAtmosphereHeightMeters());
    lua_setfield(state, -2, "atmosphere_height_meters");
    PushVec3(state, system->GetGroundAlbedoRgb());
    lua_setfield(state, -2, "ground_albedo_rgb");
    PushVec3(state, system->GetRayleighScatteringRgb());
    lua_setfield(state, -2, "rayleigh_scattering_rgb");
    lua_pushnumber(state, system->GetRayleighScaleHeightMeters());
    lua_setfield(state, -2, "rayleigh_scale_height_meters");
    PushVec3(state, system->GetMieScatteringRgb());
    lua_setfield(state, -2, "mie_scattering_rgb");
    lua_pushnumber(state, system->GetMieScaleHeightMeters());
    lua_setfield(state, -2, "mie_scale_height_meters");
    PushVec3(state, system->GetMieAbsorptionRgb());
    lua_setfield(state, -2, "mie_absorption_rgb");
    lua_pushnumber(state, system->GetMieAnisotropy());
    lua_setfield(state, -2, "mie_anisotropy");
    PushVec3(state, system->GetAbsorptionRgb());
    lua_setfield(state, -2, "ozone_absorption_rgb");
    lua_pushnumber(state, system->GetMultiScatteringFactor());
    lua_setfield(state, -2, "multi_scattering_factor");
    lua_pushboolean(state, system->GetSunDiskEnabled() ? 1 : 0);
    lua_setfield(state, -2, "sun_disk_enabled");
    lua_pushnumber(state, system->GetAerialPerspectiveDistanceScale());
    lua_setfield(state, -2, "aerial_perspective_distance_scale");
    lua_pushnumber(state, system->GetAerialScatteringStrength());
    lua_setfield(state, -2, "aerial_scattering_strength");
    return 1;
  }

  auto SkyAtmosphereSet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkyAtmosphere,
      SkyAtmosphereUserdata, CheckSkyAtmosphere>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    float fv = 0.0F;
    bool bv = false;
    Vec3 vv {};
    if (TryGetNumberField(state, 2, "planet_radius_meters", fv)) {
      system->SetPlanetRadiusMeters(fv);
    }
    if (TryGetNumberField(state, 2, "atmosphere_height_meters", fv)) {
      system->SetAtmosphereHeightMeters(fv);
    }
    lua_getfield(state, 2, "ground_albedo_rgb");
    if (lua_isvector(state, -1) != 0) {
      if (TryCheckVec3(state, -1, vv)) {
        system->SetGroundAlbedoRgb(vv);
      }
    }
    lua_pop(state, 1);
    lua_getfield(state, 2, "rayleigh_scattering_rgb");
    if (lua_isvector(state, -1) != 0) {
      if (TryCheckVec3(state, -1, vv)) {
        system->SetRayleighScatteringRgb(vv);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "rayleigh_scale_height_meters", fv)) {
      system->SetRayleighScaleHeightMeters(fv);
    }
    lua_getfield(state, 2, "mie_scattering_rgb");
    if (lua_isvector(state, -1) != 0) {
      if (TryCheckVec3(state, -1, vv)) {
        system->SetMieScatteringRgb(vv);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "mie_scale_height_meters", fv)) {
      system->SetMieScaleHeightMeters(fv);
    }
    lua_getfield(state, 2, "mie_absorption_rgb");
    if (lua_isvector(state, -1) != 0) {
      if (TryCheckVec3(state, -1, vv)) {
        system->SetMieAbsorptionRgb(vv);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "mie_anisotropy", fv)) {
      system->SetMieAnisotropy(fv);
    }
    lua_getfield(state, 2, "ozone_absorption_rgb");
    if (lua_isvector(state, -1) != 0) {
      if (TryCheckVec3(state, -1, vv)) {
        system->SetOzoneAbsorptionRgb(vv);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "multi_scattering_factor", fv)) {
      system->SetMultiScatteringFactor(fv);
    }
    if (TryGetBoolField(state, 2, "sun_disk_enabled", bv)) {
      system->SetSunDiskEnabled(bv);
    }
    if (TryGetNumberField(state, 2, "aerial_perspective_distance_scale", fv)) {
      system->SetAerialPerspectiveDistanceScale(fv);
    }
    if (TryGetNumberField(state, 2, "aerial_scattering_strength", fv)) {
      system->SetAerialScatteringStrength(fv);
    }
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SkyLightGet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkyLight,
      SkyLightUserdata, CheckSkyLight>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 6); // NOLINT(*-magic-numbers)
    const char* source_name = system->GetSource()
        == scene::environment::SkyLightSource::kCapturedScene
      ? "captured_scene"
      : "specified_cubemap";
    lua_pushstring(state, source_name);
    lua_setfield(state, -2, "source");
    lua_pushinteger(
      state, static_cast<lua_Integer>(system->GetCubemapResource().get()));
    lua_setfield(state, -2, "cubemap_resource_key");
    lua_pushnumber(state, system->GetIntensityMul());
    lua_setfield(state, -2, "intensity_mul");
    PushVec3(state, system->GetTintRgb());
    lua_setfield(state, -2, "tint_rgb");
    lua_pushnumber(state, system->GetDiffuseIntensity());
    lua_setfield(state, -2, "diffuse_intensity");
    lua_pushnumber(state, system->GetSpecularIntensity());
    lua_setfield(state, -2, "specular_intensity");
    return 1;
  }

  auto SkyLightSet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkyLight,
      SkyLightUserdata, CheckSkyLight>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_getfield(state, 2, "source");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* src = lua_tolstring(state, -1, &len);
      const std::string_view v(src, len);
      if (v == "captured_scene") {
        system->SetSource(scene::environment::SkyLightSource::kCapturedScene);
      } else if (v == "specified_cubemap") {
        system->SetSource(
          scene::environment::SkyLightSource::kSpecifiedCubemap);
      }
    }
    lua_pop(state, 1);
    std::uint64_t key = 0;
    if (TryGetIntegerField(state, 2, "cubemap_resource_key", key)) {
      system->SetCubemapResource(content::ResourceKey(key));
    }
    float fv = 0.0F;
    if (TryGetNumberField(state, 2, "intensity_mul", fv)) {
      system->SetIntensityMul(fv);
    }
    lua_getfield(state, 2, "tint_rgb");
    if (lua_isvector(state, -1) != 0) {
      Vec3 tint {};
      if (TryCheckVec3(state, -1, tint)) {
        system->SetTintRgb(tint);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "diffuse_intensity", fv)) {
      system->SetDiffuseIntensity(fv);
    }
    if (TryGetNumberField(state, 2, "specular_intensity", fv)) {
      system->SetSpecularIntensity(fv);
    }
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SkySphereGet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkySphere,
      SkySphereUserdata, CheckSkySphere>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 7); // NOLINT(*-magic-numbers)
    const char* source_name
      = system->GetSource() == scene::environment::SkySphereSource::kCubemap
      ? "cubemap"
      : "solid_color";
    lua_pushstring(state, source_name);
    lua_setfield(state, -2, "source");
    lua_pushinteger(
      state, static_cast<lua_Integer>(system->GetCubemapResource().get()));
    lua_setfield(state, -2, "cubemap_resource_key");
    PushVec3(state, system->GetSolidColorRgb());
    lua_setfield(state, -2, "solid_color_rgb");
    lua_pushnumber(state, system->GetIntensity());
    lua_setfield(state, -2, "intensity");
    lua_pushnumber(state, system->GetRotationRadians());
    lua_setfield(state, -2, "rotation_radians");
    PushVec3(state, system->GetTintRgb());
    lua_setfield(state, -2, "tint_rgb");
    return 1;
  }

  auto SkySphereSet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::SkySphere,
      SkySphereUserdata, CheckSkySphere>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_getfield(state, 2, "source");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* src = lua_tolstring(state, -1, &len);
      const std::string_view v(src, len);
      if (v == "cubemap") {
        system->SetSource(scene::environment::SkySphereSource::kCubemap);
      } else if (v == "solid_color") {
        system->SetSource(scene::environment::SkySphereSource::kSolidColor);
      }
    }
    lua_pop(state, 1);
    std::uint64_t key = 0;
    if (TryGetIntegerField(state, 2, "cubemap_resource_key", key)) {
      system->SetCubemapResource(content::ResourceKey(key));
    }
    lua_getfield(state, 2, "solid_color_rgb");
    if (lua_isvector(state, -1) != 0) {
      Vec3 color {};
      if (TryCheckVec3(state, -1, color)) {
        system->SetSolidColorRgb(color);
      }
    }
    lua_pop(state, 1);
    float fv = 0.0F;
    if (TryGetNumberField(state, 2, "intensity", fv)) {
      system->SetIntensity(fv);
    }
    if (TryGetNumberField(state, 2, "rotation_radians", fv)) {
      system->SetRotationRadians(fv);
    }
    lua_getfield(state, 2, "tint_rgb");
    if (lua_isvector(state, -1) != 0) {
      Vec3 tint {};
      if (TryCheckVec3(state, -1, tint)) {
        system->SetTintRgb(tint);
      }
    }
    lua_pop(state, 1);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SunGet(lua_State* state) -> int
  {
    const auto system
      = ResolveSystem<scene::environment::Sun, SunUserdata, CheckSun>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 11); // NOLINT(*-magic-numbers)
    const char* source_name
      = system->GetSunSource() == scene::environment::SunSource::kSynthetic
      ? "synthetic"
      : "from_scene";
    lua_pushstring(state, source_name);
    lua_setfield(state, -2, "source");
    PushVec3(state, system->GetDirectionWs());
    lua_setfield(state, -2, "direction_ws");
    lua_pushnumber(state, system->GetAzimuthDegrees());
    lua_setfield(state, -2, "azimuth_degrees");
    lua_pushnumber(state, system->GetElevationDegrees());
    lua_setfield(state, -2, "elevation_degrees");
    PushVec3(state, system->GetColorRgb());
    lua_setfield(state, -2, "color_rgb");
    lua_pushnumber(state, system->GetIlluminanceLx());
    lua_setfield(state, -2, "illuminance_lx");
    lua_pushnumber(state, system->GetDiskAngularRadiusRadians());
    lua_setfield(state, -2, "disk_angular_radius_radians");
    lua_pushboolean(state, system->CastsShadows() ? 1 : 0);
    lua_setfield(state, -2, "casts_shadows");
    if (system->HasLightTemperature()) {
      lua_pushnumber(state, system->GetLightTemperatureKelvin());
      lua_setfield(state, -2, "light_temperature_kelvin");
    }
    return 1;
  }

  auto SunSet(lua_State* state) -> int
  {
    const auto system
      = ResolveSystem<scene::environment::Sun, SunUserdata, CheckSun>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_getfield(state, 2, "source");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* src = lua_tolstring(state, -1, &len);
      const std::string_view v(src, len);
      if (v == "synthetic") {
        system->SetSunSource(scene::environment::SunSource::kSynthetic);
      } else if (v == "from_scene") {
        system->SetSunSource(scene::environment::SunSource::kFromScene);
      }
    }
    lua_pop(state, 1);
    lua_getfield(state, 2, "direction_ws");
    if (lua_isvector(state, -1) != 0) {
      Vec3 direction {};
      if (TryCheckVec3(state, -1, direction)) {
        system->SetDirectionWs(direction);
      }
    }
    lua_pop(state, 1);
    float azimuth = 0.0F;
    float elevation = 0.0F;
    const bool has_azimuth
      = TryGetNumberField(state, 2, "azimuth_degrees", azimuth);
    const bool has_elevation
      = TryGetNumberField(state, 2, "elevation_degrees", elevation);
    if (has_azimuth && has_elevation) {
      system->SetAzimuthElevationDegrees(azimuth, elevation);
    }
    lua_getfield(state, 2, "color_rgb");
    if (lua_isvector(state, -1) != 0) {
      Vec3 color {};
      if (TryCheckVec3(state, -1, color)) {
        system->SetColorRgb(color);
      }
    }
    lua_pop(state, 1);
    float fv = 0.0F;
    if (TryGetNumberField(state, 2, "illuminance_lx", fv)) {
      system->SetIlluminanceLx(fv);
    }
    if (TryGetNumberField(state, 2, "disk_angular_radius_radians", fv)) {
      system->SetDiskAngularRadiusRadians(fv);
    }
    bool bv = false;
    if (TryGetBoolField(state, 2, "casts_shadows", bv)) {
      system->SetCastsShadows(bv);
    }
    lua_getfield(state, 2, "light_temperature_kelvin");
    if (lua_isnil(state, -1) != 0) {
      system->ClearLightTemperature();
    } else if (lua_isnumber(state, -1) != 0) {
      system->SetLightTemperatureKelvin(
        static_cast<float>(lua_tonumber(state, -1)));
    }
    lua_pop(state, 1);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto CloudsGet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::VolumetricClouds,
      CloudsUserdata, CheckClouds>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 9); // NOLINT(*-magic-numbers)
    lua_pushnumber(state, system->GetBaseAltitudeMeters());
    lua_setfield(state, -2, "base_altitude_meters");
    lua_pushnumber(state, system->GetLayerThicknessMeters());
    lua_setfield(state, -2, "layer_thickness_meters");
    lua_pushnumber(state, system->GetCoverage());
    lua_setfield(state, -2, "coverage");
    lua_pushnumber(state, system->GetExtinctionSigmaTPerMeter());
    lua_setfield(state, -2, "extinction_sigma_t_per_meter");
    PushVec3(state, system->GetSingleScatteringAlbedoRgb());
    lua_setfield(state, -2, "single_scattering_albedo_rgb");
    lua_pushnumber(state, system->GetPhaseAnisotropy());
    lua_setfield(state, -2, "phase_anisotropy");
    PushVec3(state, system->GetWindDirectionWs());
    lua_setfield(state, -2, "wind_direction_ws");
    lua_pushnumber(state, system->GetWindSpeedMps());
    lua_setfield(state, -2, "wind_speed_mps");
    lua_pushnumber(state, system->GetShadowStrength());
    lua_setfield(state, -2, "shadow_strength");
    return 1;
  }

  auto CloudsSet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::VolumetricClouds,
      CloudsUserdata, CheckClouds>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    float fv = 0.0F;
    if (TryGetNumberField(state, 2, "base_altitude_meters", fv)) {
      system->SetBaseAltitudeMeters(fv);
    }
    if (TryGetNumberField(state, 2, "layer_thickness_meters", fv)) {
      system->SetLayerThicknessMeters(fv);
    }
    if (TryGetNumberField(state, 2, "coverage", fv)) {
      system->SetCoverage(fv);
    }
    if (TryGetNumberField(state, 2, "extinction_sigma_t_per_meter", fv)) {
      system->SetExtinctionSigmaTPerMeter(fv);
    }
    lua_getfield(state, 2, "single_scattering_albedo_rgb");
    if (lua_isvector(state, -1) != 0) {
      Vec3 albedo {};
      if (TryCheckVec3(state, -1, albedo)) {
        system->SetSingleScatteringAlbedoRgb(albedo);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "phase_anisotropy", fv)) {
      system->SetPhaseAnisotropy(fv);
    }
    lua_getfield(state, 2, "wind_direction_ws");
    if (lua_isvector(state, -1) != 0) {
      Vec3 wind {};
      if (TryCheckVec3(state, -1, wind)) {
        system->SetWindDirectionWs(wind);
      }
    }
    lua_pop(state, 1);
    if (TryGetNumberField(state, 2, "wind_speed_mps", fv)) {
      system->SetWindSpeedMps(fv);
    }
    if (TryGetNumberField(state, 2, "shadow_strength", fv)) {
      system->SetShadowStrength(fv);
    }
    lua_pushboolean(state, 1);
    return 1;
  }

  auto PostProcessGet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::PostProcessVolume,
      PostProcessUserdata, CheckPostProcess>(state);
    if (system == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 14); // NOLINT(*-magic-numbers)
    switch (system->GetToneMapper()) {
    case engine::ToneMapper::kNone:
      lua_pushliteral(state, "none");
      break;
    case engine::ToneMapper::kAcesFitted:
      lua_pushliteral(state, "aces_fitted");
      break;
    case engine::ToneMapper::kFilmic:
      lua_pushliteral(state, "filmic");
      break;
    case engine::ToneMapper::kReinhard:
      lua_pushliteral(state, "reinhard");
      break;
    default:
      lua_pushliteral(state, "none");
      break;
    }
    lua_setfield(state, -2, "tone_mapper");
    switch (system->GetExposureMode()) {
    case engine::ExposureMode::kManual:
      lua_pushliteral(state, "manual");
      break;
    case engine::ExposureMode::kManualCamera:
      lua_pushliteral(state, "manual_camera");
      break;
    case engine::ExposureMode::kAuto:
    default:
      lua_pushliteral(state, "auto");
      break;
    }
    lua_setfield(state, -2, "exposure_mode");
    lua_pushboolean(state, system->GetExposureEnabled() ? 1 : 0);
    lua_setfield(state, -2, "exposure_enabled");
    lua_pushnumber(state, system->GetExposureCompensationEv());
    lua_setfield(state, -2, "exposure_compensation_ev");
    lua_pushnumber(state, system->GetExposureKey());
    lua_setfield(state, -2, "exposure_key");
    lua_pushnumber(state, system->GetManualExposureEv());
    lua_setfield(state, -2, "manual_exposure_ev");
    lua_pushnumber(state, system->GetAutoExposureMinEv());
    lua_setfield(state, -2, "auto_exposure_min_ev");
    lua_pushnumber(state, system->GetAutoExposureMaxEv());
    lua_setfield(state, -2, "auto_exposure_max_ev");
    lua_pushnumber(state, system->GetAutoExposureSpeedUp());
    lua_setfield(state, -2, "auto_exposure_speed_up");
    lua_pushnumber(state, system->GetAutoExposureSpeedDown());
    lua_setfield(state, -2, "auto_exposure_speed_down");
    switch (system->GetAutoExposureMeteringMode()) {
    case engine::MeteringMode::kAverage:
      lua_pushliteral(state, "average");
      break;
    case engine::MeteringMode::kCenterWeighted:
      lua_pushliteral(state, "center_weighted");
      break;
    case engine::MeteringMode::kSpot:
      lua_pushliteral(state, "spot");
      break;
    default:
      lua_pushliteral(state, "average");
      break;
    }
    lua_setfield(state, -2, "auto_exposure_metering_mode");
    lua_pushnumber(state, system->GetBloomIntensity());
    lua_setfield(state, -2, "bloom_intensity");
    lua_pushnumber(state, system->GetBloomThreshold());
    lua_setfield(state, -2, "bloom_threshold");
    lua_pushnumber(state, system->GetSaturation());
    lua_setfield(state, -2, "saturation");
    lua_pushnumber(state, system->GetContrast());
    lua_setfield(state, -2, "contrast");
    lua_pushnumber(state, system->GetVignetteIntensity());
    lua_setfield(state, -2, "vignette_intensity");
    return 1;
  }

  auto PostProcessSet(lua_State* state) -> int
  {
    const auto system = ResolveSystem<scene::environment::PostProcessVolume,
      PostProcessUserdata, CheckPostProcess>(state);
    if (system == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_getfield(state, 2, "tone_mapper");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* v = lua_tolstring(state, -1, &len);
      const std::string_view s(v, len);
      if (s == "none") {
        system->SetToneMapper(engine::ToneMapper::kNone);
      } else if (s == "aces_fitted") {
        system->SetToneMapper(engine::ToneMapper::kAcesFitted);
      } else if (s == "filmic") {
        system->SetToneMapper(engine::ToneMapper::kFilmic);
      } else if (s == "reinhard") {
        system->SetToneMapper(engine::ToneMapper::kReinhard);
      }
    }
    lua_pop(state, 1);
    lua_getfield(state, 2, "exposure_mode");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* v = lua_tolstring(state, -1, &len);
      const std::string_view s(v, len);
      if (s == "manual") {
        system->SetExposureMode(engine::ExposureMode::kManual);
      } else if (s == "manual_camera") {
        system->SetExposureMode(engine::ExposureMode::kManualCamera);
      } else if (s == "auto") {
        system->SetExposureMode(engine::ExposureMode::kAuto);
      }
    }
    lua_pop(state, 1);
    lua_getfield(state, 2, "auto_exposure_metering_mode");
    if (lua_isstring(state, -1) != 0) {
      size_t len = 0;
      const char* v = lua_tolstring(state, -1, &len);
      const std::string_view s(v, len);
      if (s == "average") {
        system->SetAutoExposureMeteringMode(engine::MeteringMode::kAverage);
      } else if (s == "center_weighted") {
        system->SetAutoExposureMeteringMode(
          engine::MeteringMode::kCenterWeighted);
      } else if (s == "spot") {
        system->SetAutoExposureMeteringMode(engine::MeteringMode::kSpot);
      }
    }
    lua_pop(state, 1);
    bool bv = false;
    if (TryGetBoolField(state, 2, "exposure_enabled", bv)) {
      system->SetExposureEnabled(bv);
    }
    float fv = 0.0F;
    if (TryGetNumberField(state, 2, "exposure_compensation_ev", fv)) {
      system->SetExposureCompensationEv(fv);
    }
    if (TryGetNumberField(state, 2, "exposure_key", fv)) {
      system->SetExposureKey(fv);
    }
    if (TryGetNumberField(state, 2, "manual_exposure_ev", fv)) {
      system->SetManualExposureEv(fv);
    }
    float min_ev = 0.0F;
    float max_ev = 0.0F;
    const bool has_min
      = TryGetNumberField(state, 2, "auto_exposure_min_ev", min_ev);
    const bool has_max
      = TryGetNumberField(state, 2, "auto_exposure_max_ev", max_ev);
    if (has_min && has_max) {
      system->SetAutoExposureRangeEv(min_ev, max_ev);
    }
    float up = 0.0F;
    float down = 0.0F;
    const bool has_up
      = TryGetNumberField(state, 2, "auto_exposure_speed_up", up);
    const bool has_down
      = TryGetNumberField(state, 2, "auto_exposure_speed_down", down);
    if (has_up && has_down) {
      system->SetAutoExposureAdaptationSpeeds(up, down);
    }
    if (TryGetNumberField(state, 2, "bloom_intensity", fv)) {
      system->SetBloomIntensity(fv);
    }
    if (TryGetNumberField(state, 2, "bloom_threshold", fv)) {
      system->SetBloomThreshold(fv);
    }
    if (TryGetNumberField(state, 2, "saturation", fv)) {
      system->SetSaturation(fv);
    }
    if (TryGetNumberField(state, 2, "contrast", fv)) {
      system->SetContrast(fv);
    }
    if (TryGetNumberField(state, 2, "vignette_intensity", fv)) {
      system->SetVignetteIntensity(fv);
    }
    lua_pushboolean(state, 1);
    return 1;
  }
} // namespace

auto RegisterSceneEnvironmentMetatables(lua_State* state) -> void
{
  luaL_newmetatable(state, kEnvironmentMetatable);
  lua_pushvalue(state, -1);
  lua_setfield(state, -2, "__index");
  constexpr std::array<luaL_Reg, 17> env_methods {
    { { .name = "systems", .func = EnvironmentSystems },
      { .name = "has_system", .func = EnvironmentHasSystem },
      { .name = "remove_system", .func = EnvironmentRemoveSystem },
      { .name = "ensure_fog", .func = EnvironmentEnsureFog },
      { .name = "fog", .func = EnvironmentGetFog },
      { .name = "ensure_sky_atmosphere",
        .func = EnvironmentEnsureSkyAtmosphere },
      { .name = "sky_atmosphere", .func = EnvironmentGetSkyAtmosphere },
      { .name = "ensure_sky_light", .func = EnvironmentEnsureSkyLight },
      { .name = "sky_light", .func = EnvironmentGetSkyLight },
      { .name = "ensure_sky_sphere", .func = EnvironmentEnsureSkySphere },
      { .name = "sky_sphere", .func = EnvironmentGetSkySphere },
      { .name = "ensure_sun", .func = EnvironmentEnsureSun },
      { .name = "sun", .func = EnvironmentGetSun },
      { .name = "ensure_clouds", .func = EnvironmentEnsureClouds },
      { .name = "clouds", .func = EnvironmentGetClouds },
      { .name = "ensure_post_process", .func = EnvironmentEnsurePostProcess },
      { .name = "post_process", .func = EnvironmentGetPostProcess } }
  };
  for (const auto& reg : env_methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, -2, reg.name);
  }
  lua_pop(state, 1);

  luaL_newmetatable(state, kFogSystemMetatable);
  lua_pushvalue(state, -1);
  lua_setfield(state, -2, "__index");
  constexpr std::array<luaL_Reg, 16> fog_methods { { { .name = "get_model",
                                                       .func = FogGetModel },
    { .name = "set_model", .func = FogSetModel },
    { .name = "get_extinction_sigma_t_per_meter",
      .func
      = FogGetFloat<&scene::environment::Fog::GetExtinctionSigmaTPerMeter> },
    { .name = "set_extinction_sigma_t_per_meter",
      .func
      = FogSetFloat<&scene::environment::Fog::SetExtinctionSigmaTPerMeter> },
    { .name = "get_height_falloff_per_meter",
      .func = FogGetFloat<&scene::environment::Fog::GetHeightFalloffPerMeter> },
    { .name = "set_height_falloff_per_meter",
      .func = FogSetFloat<&scene::environment::Fog::SetHeightFalloffPerMeter> },
    { .name = "get_height_offset_meters",
      .func = FogGetFloat<&scene::environment::Fog::GetHeightOffsetMeters> },
    { .name = "set_height_offset_meters",
      .func = FogSetFloat<&scene::environment::Fog::SetHeightOffsetMeters> },
    { .name = "get_start_distance_meters",
      .func = FogGetFloat<&scene::environment::Fog::GetStartDistanceMeters> },
    { .name = "set_start_distance_meters",
      .func = FogSetFloat<&scene::environment::Fog::SetStartDistanceMeters> },
    { .name = "get_max_opacity",
      .func = FogGetFloat<&scene::environment::Fog::GetMaxOpacity> },
    { .name = "set_max_opacity",
      .func = FogSetFloat<&scene::environment::Fog::SetMaxOpacity> },
    { .name = "get_single_scattering_albedo_rgb",
      .func = FogGetSingleScatteringAlbedoRgb },
    { .name = "set_single_scattering_albedo_rgb",
      .func = FogSetSingleScatteringAlbedoRgb },
    { .name = "get_anisotropy",
      .func = FogGetFloat<&scene::environment::Fog::GetAnisotropy> },
    { .name = "set_anisotropy",
      .func = FogSetFloat<&scene::environment::Fog::SetAnisotropy> } } };
  for (const auto& reg : fog_methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, -2, reg.name);
  }
  lua_pop(state, 1);

  auto register_methods
    = [&](const char* metatable_name, const std::array<luaL_Reg, 2>& methods) {
        luaL_newmetatable(state, metatable_name);
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");
        for (const auto& reg : methods) {
          lua_pushcclosure(state, reg.func, reg.name, 0);
          lua_setfield(state, -2, reg.name);
        }
        lua_pop(state, 1);
      };
  register_methods(kSkyAtmosphereMetatable,
    { { { .name = "get", .func = SkyAtmosphereGet },
      { .name = "set", .func = SkyAtmosphereSet } } });
  register_methods(kSkyLightMetatable,
    { { { .name = "get", .func = SkyLightGet },
      { .name = "set", .func = SkyLightSet } } });
  register_methods(kSkySphereMetatable,
    { { { .name = "get", .func = SkySphereGet },
      { .name = "set", .func = SkySphereSet } } });
  register_methods(kSunMetatable,
    { { { .name = "get", .func = SunGet },
      { .name = "set", .func = SunSet } } });
  register_methods(kCloudsMetatable,
    { { { .name = "get", .func = CloudsGet },
      { .name = "set", .func = CloudsSet } } });
  register_methods(kPostProcessMetatable,
    { { { .name = "get", .func = PostProcessGet },
      { .name = "set", .func = PostProcessSet } } });
}

auto PushSceneEnvironment(
  lua_State* state, observer_ptr<scene::Scene> scene_ref) -> int
{
  void* data = lua_newuserdata(state, sizeof(EnvironmentUserdata));
  new (data) EnvironmentUserdata { .scene_ref = scene_ref };
  luaL_getmetatable(state, kEnvironmentMetatable);
  lua_setmetatable(state, -2);
  return 1;
}

} // namespace oxygen::scripting::bindings
