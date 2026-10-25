//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/BindingTable.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
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
      lua_newuserdatatagged(state, sizeof(Vec4Userdata), kTagVec4));
    u->x = value.x;
    u->y = value.y;
    u->z = value.z;
    u->w = value.w;
    luaL_getmetatable(state, kVec4MetatableName);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto PushMat4(lua_State* state, const Mat4& value) -> int
  {
    auto* u = static_cast<Mat4Userdata*>(
      lua_newuserdatatagged(state, sizeof(Mat4Userdata), kTagMat4));
    std::memcpy(u->m.data(), glm::value_ptr(value), sizeof(float) * kMat4Size);
    luaL_getmetatable(state, kMat4MetatableName);
    lua_setmetatable(state, -2);
    return 1;
  }

  auto CheckVec4(lua_State* state, int index) -> Vec4Userdata*
  {
    if (lua_userdatatag(state, index) != kTagVec4) {
      luaL_typeerror(state, index, kVec4MetatableName);
    }
    return static_cast<Vec4Userdata*>(lua_touserdata(state, index));
  }

  auto CheckQuat(lua_State* state, int index) -> QuatUserdata*
  {
    if (lua_userdatatag(state, index) != kTagQuat) {
      luaL_typeerror(state, index, kQuatMetatableName);
    }
    return static_cast<QuatUserdata*>(lua_touserdata(state, index));
  }

  auto CheckMat4(lua_State* state, int index) -> Mat4Userdata*
  {
    if (lua_userdatatag(state, index) != kTagMat4) {
      luaL_typeerror(state, index, kMat4MetatableName);
    }
    return static_cast<Mat4Userdata*>(lua_touserdata(state, index));
  }

  auto ToGlmQuat(const QuatUserdata* u) -> Quat
  {
    // GLM uses w, x, y, z constructor
    return { u->w, u->x, u->y, u->z };
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
    if ((data != nullptr) && (lua_getmetatable(state, 2) != 0))
    // Check for Quat * Quat
    {
      lua_getfield(state, LUA_REGISTRYINDEX, kQuatMetatableName);
      bool is_quat = lua_rawequal(state, -1, -2) != 0;
      lua_pop(state, 2);
      if (is_quat) {
        auto* q2 = static_cast<QuatUserdata*>(data);
        return PushQuat(state, ToGlmQuat(q) * ToGlmQuat(q2));
      }
    }

    if (lua_isvector(state, 2)) {
      const float* v = lua_tovector(state, 2);
      std::span<const float> span(v, 3);
      Vec3 vec(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ));
      Vec3 res = ToGlmQuat(q) * vec;
      return PushVec3(state, res);
    }

    (void)luaL_error(
      state, "oxygen.math.quat_mul expects quaternion or vector as arg #2");
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
    if (!lua_isvector(state, 2)) {
      (void)luaL_error(
        state, "oxygen.math.rotate_vec3 expects vector as arg #2");
    }
    const float* v = lua_tovector(state, 2);
    std::span<const float> span(v, 3);
    Vec3 res = ToGlmQuat(q)
      * Vec3(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ));
    return PushVec3(state, res);
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
    if (lua_gettop(state) >= 1) {
      if (!lua_isvector(state, 1)) {
        (void)luaL_error(
          state, "oxygen.math.mat4_trs expects vector as arg #1");
      }
      const float* v = lua_tovector(state, 1);
      std::span<const float> span(v, 3);
      t = Vec3(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ));
    }
    if (lua_gettop(state) >= 2) {
      auto* q = CheckQuat(state, 2);
      r = ToGlmQuat(q);
    }
    if (lua_gettop(state) >= 3) {
      if (!lua_isvector(state, 3)) {
        (void)luaL_error(
          state, "oxygen.math.mat4_trs expects vector as arg #3");
      }
      const float* v = lua_tovector(state, 3);
      std::span<const float> span(v, 3);
      s = Vec3(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ));
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
    if (!lua_isvector(state, 2)) {
      (void)luaL_error(
        state, "oxygen.math.mat4_transform_point expects vector as arg #2");
    }
    const float* v = lua_tovector(state, 2);
    std::span<const float> span(v, 3);
    Vec4 res = ToGlmMat4(m)
      * Vec4(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ),
        1.0F);
    return PushVec3(state, Vec3(res));
  }

  auto LuaMathMat4TransformDirection(lua_State* state) -> int
  {
    auto* m = CheckMat4(state, 1);
    if (!lua_isvector(state, 2)) {
      (void)luaL_error(
        state, "oxygen.math.mat4_transform_direction expects vector as arg #2");
    }
    const float* v = lua_tovector(state, 2);
    std::span<const float> span(v, 3);
    Vec4 res = ToGlmMat4(m)
      * Vec4(base::CheckedAt(span, kVecIndexX),
        base::CheckedAt(span, kVecIndexY), base::CheckedAt(span, kVecIndexZ),
        0.0F);
    return PushVec3(state, Vec3(res));
  }

  auto LuaMathMat4LookAtRh(lua_State* state) -> int
  {
    if (!lua_isvector(state, 1)) {
      (void)luaL_error(
        state, "oxygen.math.mat4_look_at_rh expects vector as arg #1");
    }
    if (!lua_isvector(state, 2)) {
      (void)luaL_error(
        state, "oxygen.math.mat4_look_at_rh expects vector as arg #2");
    }
    const float* ve = lua_tovector(state, 1);
    std::span<const float> span_e(ve, 3);
    const float* vt = lua_tovector(state, 2);
    std::span<const float> span_t(vt, 3);

    Vec3 eye(base::CheckedAt(span_e, kVecIndexX),
      base::CheckedAt(span_e, kVecIndexY), base::CheckedAt(span_e, kVecIndexZ));
    Vec3 target(base::CheckedAt(span_t, kVecIndexX),
      base::CheckedAt(span_t, kVecIndexY), base::CheckedAt(span_t, kVecIndexZ));

    Vec3 up = space::move::Up;
    if (lua_gettop(state) >= 3 && lua_isvector(state, 3)) {
      const float* vu = lua_tovector(state, 3);
      std::span<const float> span_u(vu, 3);
      up = Vec3(base::CheckedAt(span_u, kVecIndexX),
        base::CheckedAt(span_u, kVecIndexY),
        base::CheckedAt(span_u, kVecIndexZ));
    } else if (lua_gettop(state) >= 3) {
      (void)luaL_error(
        state, "oxygen.math.mat4_look_at_rh expects vector as arg #3");
    }
    return PushMat4(state, glm::lookAtRH(eye, target, up));
  }

  auto GetUserdata(lua_State* state, int index, int tag) -> void*
  {
    if (lua_userdatatag(state, index) == tag) {
      return lua_touserdata(state, index);
    }
    return nullptr;
  }

  auto LuaMathIsFinite(lua_State* state) -> int
  {
    if (lua_isvector(state, 1)) {
      const float* v = lua_tovector(state, 1);
      std::span<const float> span(v, 3);
      const bool finite = std::ranges::all_of(
        span, [](const float component) { return std::isfinite(component); });
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (lua_isnumber(state, 1) != 0) {
      lua_pushboolean(state, std::isfinite(lua_tonumber(state, 1)) ? 1 : 0);
      return 1;
    }

    if (auto* v = static_cast<Vec4Userdata*>(GetUserdata(state, 1, kTagVec4))) {
      bool finite = std::isfinite(v->x) && std::isfinite(v->y)
        && std::isfinite(v->z) && std::isfinite(v->w);
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (auto* q = static_cast<QuatUserdata*>(GetUserdata(state, 1, kTagQuat))) {
      bool finite = std::isfinite(q->x) && std::isfinite(q->y)
        && std::isfinite(q->z) && std::isfinite(q->w);
      lua_pushboolean(state, finite ? 1 : 0);
      return 1;
    }
    if (auto* m = static_cast<Mat4Userdata*>(GetUserdata(state, 1, kTagMat4))) {
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

    (void)luaL_error(state,
      "oxygen.math.is_finite expects number, vector, vec4, quat, or mat4");
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
      const bool eq = std::ranges::equal(
        s1, s2, [eps](const float first, const float second) {
          return std::fabs(first - second) <= eps;
        });
      lua_pushboolean(state, eq ? 1 : 0);
      return 1;
    }

    if (auto* v1
      = static_cast<Vec4Userdata*>(GetUserdata(state, 1, kTagVec4))) {
      if (auto* v2
        = static_cast<Vec4Userdata*>(GetUserdata(state, 2, kTagVec4))) {
        bool eq = std::fabs(v1->x - v2->x) <= eps
          && std::fabs(v1->y - v2->y) <= eps && std::fabs(v1->z - v2->z) <= eps
          && std::fabs(v1->w - v2->w) <= eps;
        lua_pushboolean(state, eq ? 1 : 0);
        return 1;
      }
    }

    if (auto* q1
      = static_cast<QuatUserdata*>(GetUserdata(state, 1, kTagQuat))) {
      if (auto* q2
        = static_cast<QuatUserdata*>(GetUserdata(state, 2, kTagQuat))) {
        bool eq = std::fabs(q1->x - q2->x) <= eps
          && std::fabs(q1->y - q2->y) <= eps && std::fabs(q1->z - q2->z) <= eps
          && std::fabs(q1->w - q2->w) <= eps;
        lua_pushboolean(state, eq ? 1 : 0);
        return 1;
      }
    }

    if (auto* m1
      = static_cast<Mat4Userdata*>(GetUserdata(state, 1, kTagMat4))) {
      if (auto* m2
        = static_cast<Mat4Userdata*>(GetUserdata(state, 2, kTagMat4))) {
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

    (void)luaL_error(
      state, "oxygen.math.near_equal expects comparable numeric/math types");
  }

  constexpr auto kMathFunctions = std::to_array<BindingFunction>({
    {
      .name = "deg_to_rad",
      .function = LuaMathDegToRad,
      .signature = "(degrees: number) -> number",
    },
    {
      .name = "rad_to_deg",
      .function = LuaMathRadToDeg,
      .signature = "(radians: number) -> number",
    },
    {
      .name = "clamp01",
      .function = LuaMathClamp01,
      .signature = "(value: number) -> number",
    },
    {
      .name = "normalize_angle_rad",
      .function = LuaMathNormalizeAngleRad,
      .signature = "(radians: number) -> number",
    },
    {
      .name = "normalize_angle_deg",
      .function = LuaMathNormalizeAngleDeg,
      .signature = "(degrees: number) -> number",
    },
    {
      .name = "vec2",
      .function = LuaMathVec2,
      .signature = "(x: number, y: number) -> vector",
    },
    {
      .name = "vec3",
      .function = LuaMathVec3,
      .signature = "(x: number, y: number, z: number) -> vector",
    },
    {
      .name = "vec4",
      .function = LuaMathVec4,
      .signature = "(x: number, y: number, z: number, w: number) -> Vec4",
    },
    {
      .name = "quat",
      .function = LuaMathQuat,
      .signature = "(x: number, y: number, z: number, w: number) -> Quat",
    },
    {
      .name = "quat_normalize",
      .function = LuaMathQuatNormalize,
      .signature = "(q: Quat) -> Quat",
    },
    {
      .name = "quat_mul",
      .function = LuaMathQuatMul,
      .signature
      = "((a: Quat, b: Quat) -> Quat) & ((q: Quat, v: vector) -> vector)",
    },
    {
      .name = "quat_from_euler_xyz",
      .function = LuaMathQuatFromEulerXyz,
      .signature = "(x: number, y: number, z: number) -> Quat",
    },
    {
      .name = "euler_xyz_from_quat",
      .function = LuaMathEulerXyzFromQuat,
      .signature = "(q: Quat) -> vector",
    },
    {
      .name = "rotate_vec3",
      .function = LuaMathRotateVec3,
      .signature = "(q: Quat, v: vector) -> vector",
    },
    {
      .name = "mat4_identity",
      .function = LuaMathMat4Identity,
      .signature = "() -> Mat4",
    },
    {
      .name = "mat4_trs",
      .function = LuaMathMat4Trs,
      .signature
      = "(translation: vector?, rotation: Quat?, scale: vector?) -> Mat4",
    },
    {
      .name = "mat4_mul",
      .function = LuaMathMat4Mul,
      .signature = "(a: Mat4, b: Mat4) -> Mat4",
    },
    {
      .name = "mat4_transform_point",
      .function = LuaMathMat4TransformPoint,
      .signature = "(m: Mat4, point: vector) -> vector",
    },
    {
      .name = "mat4_transform_direction",
      .function = LuaMathMat4TransformDirection,
      .signature = "(m: Mat4, direction: vector) -> vector",
    },
    {
      .name = "mat4_look_at_rh",
      .function = LuaMathMat4LookAtRh,
      .signature = "(eye: vector, target: vector, up: vector?) -> Mat4",
    },
    {
      .name = "is_finite",
      .function = LuaMathIsFinite,
      .signature = "(value: MathValue) -> boolean",
    },
    {
      .name = "near_equal",
      .function = LuaMathNearEqual,
      .signature = "(a: MathValue, b: MathValue, epsilon: number?) -> boolean",
    },
  });

  constexpr auto kComponentProperties = std::to_array<BindingField>({
    { .name = "x", .type = "number" },
    { .name = "y", .type = "number" },
    { .name = "z", .type = "number" },
    { .name = "w", .type = "number" },
  });

  //! Operators; their signatures are full Luau types, `self` included.
  constexpr auto kQuatMetamethods = std::to_array<BindingFunction>({
    { .name = "__unm", .function = QuatUnm, .signature = "(Quat) -> Quat" },
    {
      .name = "__mul",
      .function = QuatMul,
      .signature = "((Quat, Quat) -> Quat) & ((Quat, vector) -> vector)",
    },
  });

  constexpr auto kMat4Metamethods = std::to_array<BindingFunction>({
    {
      .name = "__mul",
      .function = LuaMathMat4Mul,
      .signature = "(Mat4, Mat4) -> Mat4",
    },
  });

  constexpr auto kMathClasses = std::to_array<BindingClass>({
    {
      .name = "Vec4",
      .metatable = kVec4MetatableName,
      .properties = kComponentProperties,
    },
    {
      .name = "Quat",
      .metatable = kQuatMetatableName,
      .properties = kComponentProperties,
      .metamethods = kQuatMetamethods,
    },
    {
      .name = "Mat4",
      .metatable = kMat4MetatableName,
      .metamethods = kMat4Metamethods,
    },
  });

  constexpr auto kMathTypes = std::to_array<BindingTypeAlias>({
    {
      .name = "MathValue",
      .definition = "number | vector | Vec4 | Quat | Mat4",
    },
  });

  constexpr auto kMathDescription = std::to_array<BindingNamespaceDescription>({
    {
      .path = "math",
      .functions = kMathFunctions,
      .classes = kMathClasses,
      .types = kMathTypes,
    },
  });

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
    RegisterFunctions(state, -1, kQuatMetamethods);
  }
  lua_pop(state, 1);

  if (luaL_newmetatable(state, kMat4MetatableName) != 0) {
    lua_pushcfunction(state, Mat4ToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    RegisterFunctions(state, -1, kMat4Metamethods);
  }
  lua_pop(state, 1);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "math");
  RegisterFunctions(state, module_index, kMathFunctions);
  lua_pop(state, 1);
}

auto DescribeMathBindings() -> std::span<const BindingNamespaceDescription>
{
  return kMathDescription;
}

auto TryCheckQuat(lua_State* state, const int index) -> QuatUserdata*
{
  if (lua_type(state, index) != LUA_TUSERDATA
    || lua_userdatatag(state, index) != kTagQuat) {
    return nullptr;
  }
  return static_cast<QuatUserdata*>(lua_touserdata(state, index));
}

auto PushQuat(lua_State* state, const Quat& value) -> int
{
  auto* u = static_cast<QuatUserdata*>(
    lua_newuserdatatagged(state, sizeof(QuatUserdata), kTagQuat));
  u->x = value.x;
  u->y = value.y;
  u->z = value.z;
  u->w = value.w;
  luaL_getmetatable(state, kQuatMetatableName);
  lua_setmetatable(state, -2);
  return 1;
}

} // namespace oxygen::scripting::bindings
