//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/MathBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr const char* kVec4MetatableName = "oxygen.vec4";
  constexpr const char* kQuatMetatableName = "oxygen.quat";
  constexpr const char* kMat4MetatableName = "oxygen.mat4";

  constexpr int kLuaArg1 = 1;
  constexpr int kLuaArg2 = 2;
  constexpr int kLuaArg3 = 3;
  constexpr float kDefaultNearEqualEpsilon = oxygen::math::Epsilon;
  constexpr size_t kMat4Size = 16;

  constexpr int kVecIndexX = 0;
  constexpr int kVecIndexY = 1;
  constexpr int kVecIndexZ = 2;

  // --- Userdata Structures ---

  struct Vec4Userdata {
    float x, y, z, w;
  };

  struct QuatUserdata {
    float x, y, z, w;
  };

  struct Mat4Userdata {
    std::array<float, kMat4Size> m; // Column-major
  };

  // --- Helpers ---

  auto PushVec2(lua_State* state, const Vec2& value) -> int
  {
    // Native vector is 3-component (x, y, z). We use z=0 for Vec2.
    lua_pushvector(state, value.x, value.y, 0.0F);
    return 1;
  }

  auto PushVec3(lua_State* state, const Vec3& value) -> int
  {
    lua_pushvector(state, value.x, value.y, value.z);
    return 1;
  }

  auto PushVec4(lua_State* state, const Vec4& value) -> int
  {
    auto* u = static_cast<Vec4Userdata*>(
      lua_newuserdata(state, sizeof(Vec4Userdata)));
    u->x = value.x;
    u->y = value.y;
    u->z = value.z;
    u->w = value.w;
    luaL_getmetatable(state, kVec4MetatableName);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto PushQuat(lua_State* state, const Quat& value) -> int
  {
    auto* u = static_cast<QuatUserdata*>(
      lua_newuserdata(state, sizeof(QuatUserdata)));
    u->x = value.x;
    u->y = value.y;
    u->z = value.z;
    u->w = value.w;
    luaL_getmetatable(state, kQuatMetatableName);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto PushMat4(lua_State* state, const Mat4& value) -> int
  {
    auto* u = static_cast<Mat4Userdata*>(
      lua_newuserdata(state, sizeof(Mat4Userdata)));
    std::memcpy(u->m.data(), glm::value_ptr(value), sizeof(float) * kMat4Size);
    luaL_getmetatable(state, kMat4MetatableName);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto CheckVec4(lua_State* state, int index) -> Vec4Userdata*
  {
    return static_cast<Vec4Userdata*>(
      luaL_checkudata(state, index, kVec4MetatableName));
  }

  auto CheckQuat(lua_State* state, int index) -> QuatUserdata*
  {
    return static_cast<QuatUserdata*>(
      luaL_checkudata(state, index, kQuatMetatableName));
  }

  auto CheckMat4(lua_State* state, int index) -> Mat4Userdata*
  {
    return static_cast<Mat4Userdata*>(
      luaL_checkudata(state, index, kMat4MetatableName));
  }

  auto ToGlmQuat(const QuatUserdata* u) -> Quat
  {
    // GLM uses w, x, y, z constructor
    return Quat(u->w, u->x, u->y, u->z);
  }

  auto ToGlmMat4(const Mat4Userdata* u) -> Mat4
  {
    return glm::make_mat4(u->m.data());
  }

  // --- Metamethods ---

  auto Vec4ToString(lua_State* state) -> int
  {
    auto* v = CheckVec4(state, 1);
    std::string s = "Vec4(" + std::to_string(v->x) + ", " + std::to_string(v->y)
      + ", " + std::to_string(v->z) + ", " + std::to_string(v->w) + ")";
    lua_pushlstring(state, s.c_str(), s.size());
    return 1;
  }

  auto Vec4Index(lua_State* state) -> int
  {
    auto* v = CheckVec4(state, 1);
    size_t len = 0;
    const char* key_ptr = luaL_checklstring(state, 2, &len);
    const std::string_view key(key_ptr, len);

    if (key == "x") {
      lua_pushnumber(state, v->x);
    } else if (key == "y") {
      lua_pushnumber(state, v->y);
    } else if (key == "z") {
      lua_pushnumber(state, v->z);
    } else if (key == "w") {
      lua_pushnumber(state, v->w);
    } else {
      lua_pushnil(state);
    }
    return 1;
  }

  auto QuatToString(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    std::string s = "Quat(" + std::to_string(q->x) + ", " + std::to_string(q->y)
      + ", " + std::to_string(q->z) + ", " + std::to_string(q->w) + ")";
    lua_pushlstring(state, s.c_str(), s.size());
    return 1;
  }

  auto QuatIndex(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    size_t len = 0;
    const char* key_ptr = luaL_checklstring(state, 2, &len);
    const std::string_view key(key_ptr, len);

    if (key == "x") {
      lua_pushnumber(state, q->x);
    } else if (key == "y") {
      lua_pushnumber(state, q->y);
    } else if (key == "z") {
      lua_pushnumber(state, q->z);
    } else if (key == "w") {
      lua_pushnumber(state, q->w);
    } else {
      lua_pushnil(state);
    }
    return 1;
  }

  auto QuatUnm(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    return PushQuat(state, -ToGlmQuat(q));
  }

  auto QuatMul(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);

    void* data = lua_touserdata(state, 2);
    if (data != nullptr) {
      // Check for Quat * Quat
      if (lua_getmetatable(state, 2) != 0) {
        lua_getfield(state, LUA_REGISTRYINDEX, kQuatMetatableName);
        bool is_quat = lua_rawequal(state, -1, -2) != 0;
        lua_pop(state, 2);
        if (is_quat) {
          auto* q2 = static_cast<QuatUserdata*>(data);
          return PushQuat(state, ToGlmQuat(q) * ToGlmQuat(q2));
        }
      }
    }

    if (lua_isvector(state, 2)) {
      const float* v = lua_tovector(state, 2);
      std::span<const float> span(v, 3);
      Vec3 vec(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ]);
      Vec3 res = ToGlmQuat(q) * vec;
      return PushVec3(state, res);
    }

    return 0;
  }

  auto Mat4ToString(lua_State* state) -> int
  {
    lua_pushliteral(state, "Mat4(...)");
    return 1;
  }

  // --- Functions ---

  auto LuaMathQuat(lua_State* state) -> int
  {
    auto x = static_cast<float>(luaL_checknumber(state, 1));
    auto y = static_cast<float>(luaL_checknumber(state, 2));
    auto z = static_cast<float>(luaL_checknumber(state, 3));
    auto w = static_cast<float>(luaL_checknumber(state, 4));
    return PushQuat(state, Quat(w, x, y, z));
  }

  auto LuaMathQuatNormalize(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    return PushQuat(state, glm::normalize(ToGlmQuat(q)));
  }

  auto LuaMathQuatMul(lua_State* state) -> int { return QuatMul(state); }

  auto LuaMathVec3(lua_State* state) -> int
  {
    auto x = static_cast<float>(luaL_checknumber(state, 1));
    auto y = static_cast<float>(luaL_checknumber(state, 2));
    auto z = static_cast<float>(luaL_checknumber(state, 3));
    lua_pushvector(state, x, y, z);
    return 1;
  }

  auto NormalizeAngleRad(const float radians) -> float
  {
    return std::atan2(std::sin(radians), std::cos(radians));
  }

  auto LuaMathDegToRad(lua_State* state) -> int
  {
    auto d = static_cast<float>(luaL_checknumber(state, 1));
    lua_pushnumber(state, d * oxygen::math::DegToRad);
    return 1;
  }

  auto LuaMathRadToDeg(lua_State* state) -> int
  {
    auto r = static_cast<float>(luaL_checknumber(state, 1));
    lua_pushnumber(state, r * oxygen::math::RadToDeg);
    return 1;
  }

  auto LuaMathClamp01(lua_State* state) -> int
  {
    auto v = static_cast<float>(luaL_checknumber(state, 1));
    lua_pushnumber(state, std::clamp(v, 0.0F, 1.0F));
    return 1;
  }

  auto LuaMathNormalizeAngleRad(lua_State* state) -> int
  {
    auto r = static_cast<float>(luaL_checknumber(state, 1));
    lua_pushnumber(state, NormalizeAngleRad(r));
    return 1;
  }

  auto LuaMathNormalizeAngleDeg(lua_State* state) -> int
  {
    auto d = static_cast<float>(luaL_checknumber(state, 1));
    float r = d * oxygen::math::DegToRad;
    lua_pushnumber(state, NormalizeAngleRad(r) * oxygen::math::RadToDeg);
    return 1;
  }

  auto LuaMathVec2(lua_State* state) -> int
  {
    auto x = static_cast<float>(luaL_checknumber(state, 1));
    auto y = static_cast<float>(luaL_checknumber(state, 2));
    return PushVec2(state, Vec2(x, y));
  }

  auto LuaMathVec4(lua_State* state) -> int
  {
    auto x = static_cast<float>(luaL_checknumber(state, 1));
    auto y = static_cast<float>(luaL_checknumber(state, 2));
    auto z = static_cast<float>(luaL_checknumber(state, 3));
    auto w = static_cast<float>(luaL_checknumber(state, 4));
    return PushVec4(state, Vec4(x, y, z, w));
  }

  auto LuaMathQuatFromEulerXyz(lua_State* state) -> int
  {
    auto x = static_cast<float>(luaL_checknumber(state, 1));
    auto y = static_cast<float>(luaL_checknumber(state, 2));
    auto z = static_cast<float>(luaL_checknumber(state, 3));
    return PushQuat(state, glm::quat(Vec3(x, y, z)));
  }

  auto LuaMathEulerXyzFromQuat(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    return PushVec3(state, glm::eulerAngles(ToGlmQuat(q)));
  }

  auto LuaMathRotateVec3(lua_State* state) -> int
  {
    auto* q = CheckQuat(state, 1);
    if (lua_isvector(state, 2)) {
      const float* v = lua_tovector(state, 2);
      std::span<const float> span(v, 3);
      Vec3 res = ToGlmQuat(q)
        * Vec3(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ]);
      return PushVec3(state, res);
    }
    return 0;
  }

  auto LuaMathMat4Identity(lua_State* state) -> int
  {
    return PushMat4(state, Mat4(1.0F));
  }

  auto LuaMathMat4Trs(lua_State* state) -> int
  {
    Vec3 t(0);
    Vec3 s(1);
    Quat r {};
    if (lua_gettop(state) >= 1 && lua_isvector(state, 1)) {
      const float* v = lua_tovector(state, 1);
      std::span<const float> span(v, 3);
      t = Vec3(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ]);
    }
    if (lua_gettop(state) >= 2) {
      auto* q = CheckQuat(state, 2);
      r = ToGlmQuat(q);
    }
    if (lua_gettop(state) >= 3 && lua_isvector(state, 3)) {
      const float* v = lua_tovector(state, 3);
      std::span<const float> span(v, 3);
      s = Vec3(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ]);
    }

    const Mat4 transform = glm::translate(Mat4(1.0F), t) * glm::mat4_cast(r)
      * glm::scale(Mat4(1.0F), s);
    return PushMat4(state, transform);
  }

  auto LuaMathMat4Mul(lua_State* state) -> int
  {
    auto* lhs = CheckMat4(state, 1);
    auto* rhs = CheckMat4(state, 2);
    return PushMat4(state, ToGlmMat4(lhs) * ToGlmMat4(rhs));
  }

  auto LuaMathMat4TransformPoint(lua_State* state) -> int
  {
    auto* m = CheckMat4(state, 1);
    const float* v = lua_tovector(state, 2);
    std::span<const float> span(v, 3);
    Vec4 res = ToGlmMat4(m)
      * Vec4(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ], 1.0F);
    return PushVec3(state, Vec3(res));
  }

  auto LuaMathMat4TransformDirection(lua_State* state) -> int
  {
    auto* m = CheckMat4(state, 1);
    const float* v = lua_tovector(state, 2);
    std::span<const float> span(v, 3);
    Vec4 res = ToGlmMat4(m)
      * Vec4(span[kVecIndexX], span[kVecIndexY], span[kVecIndexZ], 0.0F);
    return PushVec3(state, Vec3(res));
  }

  auto LuaMathMat4LookAtRh(lua_State* state) -> int
  {
    const float* ve = lua_tovector(state, 1);
    std::span<const float> span_e(ve, 3);
    const float* vt = lua_tovector(state, 2);
    std::span<const float> span_t(vt, 3);

    Vec3 eye(span_e[kVecIndexX], span_e[kVecIndexY], span_e[kVecIndexZ]);
    Vec3 target(span_t[kVecIndexX], span_t[kVecIndexY], span_t[kVecIndexZ]);

    Vec3 up = space::move::Up;
    if (lua_gettop(state) >= 3 && lua_isvector(state, 3)) {
      const float* vu = lua_tovector(state, 3);
      std::span<const float> span_u(vu, 3);
      up = Vec3(span_u[kVecIndexX], span_u[kVecIndexY], span_u[kVecIndexZ]);
    }
    return PushMat4(state, glm::lookAtRH(eye, target, up));
  }

  auto GetUserdata(lua_State* state, int index, const char* meta_name) -> void*
  {
    void* data = lua_touserdata(state, index);
    if (data != nullptr) {
      if (lua_getmetatable(state, index) != 0) {
        lua_getfield(state, LUA_REGISTRYINDEX, meta_name);
        if (lua_rawequal(state, -1, -2) != 0) {
          lua_pop(state, 2);
          return data;
        }
        lua_pop(state, 2);
      }
    }
    return nullptr;
  }

  auto LuaMathIsFinite(lua_State* state) -> int
  {
    if (lua_isvector(state, 1)) {
      const float* v = lua_tovector(state, 1);
      std::span<const float> span(v, 3);
      bool finite = std::isfinite(span[0]) && std::isfinite(span[1])
        && std::isfinite(span[2]);
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (lua_isnumber(state, 1) != 0) {
      lua_pushboolean(state, std::isfinite(lua_tonumber(state, 1)) ? 1 : 0);
      return 1;
    }

    if (auto* v
      = static_cast<Vec4Userdata*>(GetUserdata(state, 1, kVec4MetatableName))) {
      bool finite = std::isfinite(v->x) && std::isfinite(v->y)
        && std::isfinite(v->z) && std::isfinite(v->w);
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (auto* q
      = static_cast<QuatUserdata*>(GetUserdata(state, 1, kQuatMetatableName))) {
      bool finite = std::isfinite(q->x) && std::isfinite(q->y)
        && std::isfinite(q->z) && std::isfinite(q->w);
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (auto* m
      = static_cast<Mat4Userdata*>(GetUserdata(state, 1, kMat4MetatableName))) {
      bool finite = true;
      for (const float f : m->m) {
        if (!std::isfinite(f)) {
          finite = false;
          break;
        }
      }
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }

    lua_pushboolean(state, 0);
    return 1;
  }

  auto LuaMathNearEqual(lua_State* state) -> int
  {
    float eps = kDefaultNearEqualEpsilon;
    if (lua_isnumber(state, kLuaArg3) != 0) {
      eps = static_cast<float>(lua_tonumber(state, kLuaArg3));
    }

    if (lua_isnumber(state, kLuaArg1) != 0
      && lua_isnumber(state, kLuaArg2) != 0) {
      auto v1 = static_cast<float>(lua_tonumber(state, kLuaArg1));
      auto v2 = static_cast<float>(lua_tonumber(state, kLuaArg2));
      lua_pushboolean(state, std::fabs(v1 - v2) <= eps ? 1 : 0);
      return 1;
    }

    if (lua_isvector(state, kLuaArg1) && lua_isvector(state, kLuaArg2)) {
      const float* v1 = lua_tovector(state, kLuaArg1);
      const float* v2 = lua_tovector(state, kLuaArg2);
      std::span<const float> s1(v1, 3);
      std::span<const float> s2(v2, 3);
      // Check 3 components
      bool eq = std::fabs(s1[0] - s2[0]) <= eps
        && std::fabs(s1[1] - s2[1]) <= eps && std::fabs(s1[2] - s2[2]) <= eps;
      lua_pushboolean(state, eq ? 1 : 0);
      return 1;
    }

    if (auto* v1
      = static_cast<Vec4Userdata*>(GetUserdata(state, 1, kVec4MetatableName))) {
      if (auto* v2 = static_cast<Vec4Userdata*>(
            GetUserdata(state, 2, kVec4MetatableName))) {
        bool eq = std::fabs(v1->x - v2->x) <= eps
          && std::fabs(v1->y - v2->y) <= eps && std::fabs(v1->z - v2->z) <= eps
          && std::fabs(v1->w - v2->w) <= eps;
        lua_pushboolean(state, eq ? 1 : 0);
        return 1;
      }
    }

    if (auto* q1
      = static_cast<QuatUserdata*>(GetUserdata(state, 1, kQuatMetatableName))) {
      if (auto* q2 = static_cast<QuatUserdata*>(
            GetUserdata(state, 2, kQuatMetatableName))) {
        bool eq = std::fabs(q1->x - q2->x) <= eps
          && std::fabs(q1->y - q2->y) <= eps && std::fabs(q1->z - q2->z) <= eps
          && std::fabs(q1->w - q2->w) <= eps;
        lua_pushboolean(state, eq ? 1 : 0);
        return 1;
      }
    }

    if (auto* m1
      = static_cast<Mat4Userdata*>(GetUserdata(state, 1, kMat4MetatableName))) {
      if (auto* m2 = static_cast<Mat4Userdata*>(
            GetUserdata(state, 2, kMat4MetatableName))) {
        bool eq = true;
        for (size_t i = 0; i < kMat4Size; ++i) {
          if (std::fabs(m1->m.at(i) - m2->m.at(i)) > eps) {
            eq = false;
            break;
          }
        }
        lua_pushboolean(state, eq ? 1 : 0);
        return 1;
      }
    }

    lua_pushboolean(state, 0);
    return 1;
  }

} // namespace

auto RegisterMathBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  if (luaL_newmetatable(state, kVec4MetatableName) != 0) {
    lua_pushcfunction(state, Vec4ToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, Vec4Index, "__index");
    lua_setfield(state, -2, "__index");
  }
  lua_pop(state, 1);

  if (luaL_newmetatable(state, kQuatMetatableName) != 0) {
    lua_pushcfunction(state, QuatToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, QuatIndex, "__index");
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, QuatUnm, "__unm");
    lua_setfield(state, -2, "__unm");
    lua_pushcfunction(state, QuatMul, "__mul");
    lua_setfield(state, -2, "__mul");
  }
  lua_pop(state, 1);

  if (luaL_newmetatable(state, kMat4MetatableName) != 0) {
    lua_pushcfunction(state, Mat4ToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, LuaMathMat4Mul, "__mul");
    lua_setfield(state, -2, "__mul");
  }
  lua_pop(state, 1);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "math");

  lua_pushcfunction(state, LuaMathDegToRad, "math.deg_to_rad");
  lua_setfield(state, module_index, "deg_to_rad");
  lua_pushcfunction(state, LuaMathRadToDeg, "math.rad_to_deg");
  lua_setfield(state, module_index, "rad_to_deg");
  lua_pushcfunction(state, LuaMathClamp01, "math.clamp01");
  lua_setfield(state, module_index, "clamp01");
  lua_pushcfunction(
    state, LuaMathNormalizeAngleRad, "math.normalize_angle_rad");
  lua_setfield(state, module_index, "normalize_angle_rad");
  lua_pushcfunction(
    state, LuaMathNormalizeAngleDeg, "math.normalize_angle_deg");
  lua_setfield(state, module_index, "normalize_angle_deg");

  lua_pushcfunction(state, LuaMathVec2, "math.vec2");
  lua_setfield(state, module_index, "vec2");
  lua_pushcfunction(state, LuaMathVec3, "math.vec3");
  lua_setfield(state, module_index, "vec3");
  lua_pushcfunction(state, LuaMathVec4, "math.vec4");
  lua_setfield(state, module_index, "vec4");

  lua_pushcfunction(state, LuaMathQuat, "math.quat");
  lua_setfield(state, module_index, "quat");
  lua_pushcfunction(state, LuaMathQuatNormalize, "math.quat_normalize");
  lua_setfield(state, module_index, "quat_normalize");
  lua_pushcfunction(state, LuaMathQuatMul, "math.quat_mul");
  lua_setfield(state, module_index, "quat_mul");
  lua_pushcfunction(state, LuaMathQuatFromEulerXyz, "math.quat_from_euler_xyz");
  lua_setfield(state, module_index, "quat_from_euler_xyz");
  lua_pushcfunction(state, LuaMathEulerXyzFromQuat, "math.euler_xyz_from_quat");
  lua_setfield(state, module_index, "euler_xyz_from_quat");
  lua_pushcfunction(state, LuaMathRotateVec3, "math.rotate_vec3");
  lua_setfield(state, module_index, "rotate_vec3");

  lua_pushcfunction(state, LuaMathMat4Identity, "math.mat4_identity");
  lua_setfield(state, module_index, "mat4_identity");
  lua_pushcfunction(state, LuaMathMat4Trs, "math.mat4_trs");
  lua_setfield(state, module_index, "mat4_trs");
  lua_pushcfunction(state, LuaMathMat4Mul, "math.mat4_mul");
  lua_setfield(state, module_index, "mat4_mul");
  lua_pushcfunction(
    state, LuaMathMat4TransformPoint, "math.mat4_transform_point");
  lua_setfield(state, module_index, "mat4_transform_point");
  lua_pushcfunction(
    state, LuaMathMat4TransformDirection, "math.mat4_transform_direction");
  lua_setfield(state, module_index, "mat4_transform_direction");
  lua_pushcfunction(state, LuaMathMat4LookAtRh, "math.mat4_look_at_rh");
  lua_setfield(state, module_index, "mat4_look_at_rh");

  lua_pushcfunction(state, LuaMathIsFinite, "math.is_finite");
  lua_setfield(state, module_index, "is_finite");
  lua_pushcfunction(state, LuaMathNearEqual, "math.near_equal");
  lua_setfield(state, module_index, "near_equal");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
