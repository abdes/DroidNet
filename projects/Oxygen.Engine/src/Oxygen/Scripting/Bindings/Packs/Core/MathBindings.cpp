//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <lua.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/MathBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArg1 = 1;
  constexpr int kLuaArg2 = 2;
  constexpr int kLuaArg3 = 3;
  [[maybe_unused]] constexpr int kLuaArg4 = 4;
  constexpr int kLuaArgX = 1;
  constexpr int kLuaArgY = 2;
  constexpr int kLuaArgZ = 3;
  constexpr int kLuaArgW = 4;
  constexpr int kLuaMatElementCount = 16;
  constexpr float kDefaultNearEqualEpsilon = oxygen::math::Epsilon;

  auto PushVec2(lua_State* state, const Vec2& value) -> int
  {
    lua_newtable(state);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    return 1;
  }

  auto PushVec3(lua_State* state, const Vec3& value) -> int
  {
    lua_newtable(state);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
    return 1;
  }

  auto PushVec4(lua_State* state, const Vec4& value) -> int
  {
    lua_newtable(state);
    lua_pushnumber(state, value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z);
    lua_setfield(state, -2, "z");
    lua_pushnumber(state, value.w);
    lua_setfield(state, -2, "w");
    return 1;
  }

  auto PushQuat(lua_State* state, const Quat& value) -> int
  {
    return PushVec4(state, Vec4 { value.x, value.y, value.z, value.w });
  }

  auto PushMat4(lua_State* state, const Mat4& value) -> int
  {
    lua_newtable(state);
    int index = 1;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        lua_pushnumber(state, value[col][row]);
        lua_rawseti(state, -2, index);
        ++index;
      }
    }
    return 1;
  }

  auto ReadFieldNumber(lua_State* state, const int index,
    const char* field_name, float& out) -> bool
  {
    lua_getfield(state, index, field_name);
    if (lua_isnumber(state, -1) == 0) {
      lua_pop(state, 1);
      return false;
    }
    out = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
    return true;
  }

  auto ReadVec3(lua_State* state, const int index, Vec3& out) -> bool
  {
    if (!lua_istable(state, index)) {
      return false;
    }
    return ReadFieldNumber(state, index, "x", out.x)
      && ReadFieldNumber(state, index, "y", out.y)
      && ReadFieldNumber(state, index, "z", out.z);
  }

  auto ReadQuat(lua_State* state, const int index, Quat& out) -> bool
  {
    if (!lua_istable(state, index)) {
      return false;
    }
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
    if (!ReadFieldNumber(state, index, "x", x)
      || !ReadFieldNumber(state, index, "y", y)
      || !ReadFieldNumber(state, index, "z", z)
      || !ReadFieldNumber(state, index, "w", w)) {
      return false;
    }
    out = Quat { w, x, y, z };
    return true;
  }

  auto ReadMat4(lua_State* state, const int index, Mat4& out) -> bool
  {
    if (!lua_istable(state, index)) {
      return false;
    }
    int element = 1;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        lua_rawgeti(state, index, element);
        if (lua_isnumber(state, -1) == 0) {
          lua_pop(state, 1);
          return false;
        }
        out[col][row] = static_cast<float>(lua_tonumber(state, -1));
        lua_pop(state, 1);
        ++element;
      }
    }
    return true;
  }

  auto NormalizeAngleRad(const float radians) -> float
  {
    return std::atan2(std::sin(radians), std::cos(radians));
  }

  auto LuaMathDegToRad(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) == 0) {
      return 0;
    }
    const float degrees = static_cast<float>(lua_tonumber(state, kLuaArg1));
    lua_pushnumber(state, degrees * oxygen::math::DegToRad);
    return 1;
  }

  auto LuaMathRadToDeg(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) == 0) {
      return 0;
    }
    const float radians = static_cast<float>(lua_tonumber(state, kLuaArg1));
    lua_pushnumber(state, radians * oxygen::math::RadToDeg);
    return 1;
  }

  auto LuaMathClamp01(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) == 0) {
      return 0;
    }
    const float value = static_cast<float>(lua_tonumber(state, kLuaArg1));
    lua_pushnumber(state, std::clamp(value, 0.0F, 1.0F));
    return 1;
  }

  auto LuaMathNormalizeAngleRad(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) == 0) {
      return 0;
    }
    const float radians = static_cast<float>(lua_tonumber(state, kLuaArg1));
    lua_pushnumber(state, NormalizeAngleRad(radians));
    return 1;
  }

  auto LuaMathNormalizeAngleDeg(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) == 0) {
      return 0;
    }
    const float degrees = static_cast<float>(lua_tonumber(state, kLuaArg1));
    const float radians = degrees * oxygen::math::DegToRad;
    lua_pushnumber(state, NormalizeAngleRad(radians) * oxygen::math::RadToDeg);
    return 1;
  }

  auto LuaMathVec2(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArgX) == 0
      || lua_isnumber(state, kLuaArgY) == 0) {
      return 0;
    }
    const Vec2 value {
      static_cast<float>(lua_tonumber(state, kLuaArgX)),
      static_cast<float>(lua_tonumber(state, kLuaArgY)),
    };
    return PushVec2(state, value);
  }

  auto LuaMathVec3(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArgX) == 0 || lua_isnumber(state, kLuaArgY) == 0
      || lua_isnumber(state, kLuaArgZ) == 0) {
      return 0;
    }
    const Vec3 value {
      static_cast<float>(lua_tonumber(state, kLuaArgX)),
      static_cast<float>(lua_tonumber(state, kLuaArgY)),
      static_cast<float>(lua_tonumber(state, kLuaArgZ)),
    };
    return PushVec3(state, value);
  }

  auto LuaMathVec4(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArgX) == 0 || lua_isnumber(state, kLuaArgY) == 0
      || lua_isnumber(state, kLuaArgZ) == 0
      || lua_isnumber(state, kLuaArgW) == 0) {
      return 0;
    }
    const Vec4 value {
      static_cast<float>(lua_tonumber(state, kLuaArgX)),
      static_cast<float>(lua_tonumber(state, kLuaArgY)),
      static_cast<float>(lua_tonumber(state, kLuaArgZ)),
      static_cast<float>(lua_tonumber(state, kLuaArgW)),
    };
    return PushVec4(state, value);
  }

  auto LuaMathQuat(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArgX) == 0 || lua_isnumber(state, kLuaArgY) == 0
      || lua_isnumber(state, kLuaArgZ) == 0
      || lua_isnumber(state, kLuaArgW) == 0) {
      return 0;
    }
    const Quat value {
      static_cast<float>(lua_tonumber(state, kLuaArgW)),
      static_cast<float>(lua_tonumber(state, kLuaArgX)),
      static_cast<float>(lua_tonumber(state, kLuaArgY)),
      static_cast<float>(lua_tonumber(state, kLuaArgZ)),
    };
    return PushQuat(state, value);
  }

  auto LuaMathQuatNormalize(lua_State* state) -> int
  {
    Quat quat {};
    if (!ReadQuat(state, kLuaArg1, quat)) {
      return 0;
    }
    return PushQuat(state, glm::normalize(quat));
  }

  auto LuaMathQuatMul(lua_State* state) -> int
  {
    Quat lhs {};
    Quat rhs {};
    if (!ReadQuat(state, kLuaArg1, lhs) || !ReadQuat(state, kLuaArg2, rhs)) {
      return 0;
    }
    return PushQuat(state, lhs * rhs);
  }

  auto LuaMathQuatFromEulerXyz(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArgX) == 0 || lua_isnumber(state, kLuaArgY) == 0
      || lua_isnumber(state, kLuaArgZ) == 0) {
      return 0;
    }
    const Vec3 euler_xyz {
      static_cast<float>(lua_tonumber(state, kLuaArgX)),
      static_cast<float>(lua_tonumber(state, kLuaArgY)),
      static_cast<float>(lua_tonumber(state, kLuaArgZ)),
    };
    return PushQuat(state, glm::quat(euler_xyz));
  }

  auto LuaMathEulerXyzFromQuat(lua_State* state) -> int
  {
    Quat quat {};
    if (!ReadQuat(state, kLuaArg1, quat)) {
      return 0;
    }
    return PushVec3(state, glm::eulerAngles(quat));
  }

  auto LuaMathRotateVec3(lua_State* state) -> int
  {
    Quat quat {};
    Vec3 vec {};
    if (!ReadQuat(state, kLuaArg1, quat) || !ReadVec3(state, kLuaArg2, vec)) {
      return 0;
    }
    return PushVec3(state, quat * vec);
  }

  auto LuaMathMat4Identity(lua_State* state) -> int
  {
    return PushMat4(state, Mat4 { 1.0F });
  }

  auto LuaMathMat4Trs(lua_State* state) -> int
  {
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale {};
    if (!ReadVec3(state, kLuaArg1, translation)
      || !ReadQuat(state, kLuaArg2, rotation)
      || !ReadVec3(state, kLuaArg3, scale)) {
      return 0;
    }

    const Mat4 transform = glm::translate(Mat4 { 1.0F }, translation)
      * glm::mat4_cast(rotation) * glm::scale(Mat4 { 1.0F }, scale);
    return PushMat4(state, transform);
  }

  auto LuaMathMat4Mul(lua_State* state) -> int
  {
    Mat4 lhs { 1.0F };
    Mat4 rhs { 1.0F };
    if (!ReadMat4(state, kLuaArg1, lhs) || !ReadMat4(state, kLuaArg2, rhs)) {
      return 0;
    }
    return PushMat4(state, lhs * rhs);
  }

  auto LuaMathMat4TransformPoint(lua_State* state) -> int
  {
    Mat4 matrix { 1.0F };
    Vec3 point {};
    if (!ReadMat4(state, kLuaArg1, matrix)
      || !ReadVec3(state, kLuaArg2, point)) {
      return 0;
    }
    const Vec4 transformed = matrix * Vec4 { point, 1.0F };
    return PushVec3(
      state, Vec3 { transformed.x, transformed.y, transformed.z });
  }

  auto LuaMathMat4TransformDirection(lua_State* state) -> int
  {
    Mat4 matrix { 1.0F };
    Vec3 direction {};
    if (!ReadMat4(state, kLuaArg1, matrix)
      || !ReadVec3(state, kLuaArg2, direction)) {
      return 0;
    }
    const Vec4 transformed = matrix * Vec4 { direction, 0.0F };
    return PushVec3(
      state, Vec3 { transformed.x, transformed.y, transformed.z });
  }

  auto LuaMathMat4LookAtRh(lua_State* state) -> int
  {
    Vec3 eye {};
    Vec3 target {};
    if (!ReadVec3(state, kLuaArg1, eye) || !ReadVec3(state, kLuaArg2, target)) {
      return 0;
    }

    Vec3 up = space::move::Up;
    if (lua_istable(state, kLuaArg3) && !ReadVec3(state, kLuaArg3, up)) {
      return 0;
    }

    return PushMat4(state, glm::lookAtRH(eye, target, up));
  }

  auto LuaMathIsFinite(lua_State* state) -> int
  {
    if (lua_isnumber(state, kLuaArg1) != 0) {
      const auto value = static_cast<float>(lua_tonumber(state, kLuaArg1));
      lua_pushboolean(state, std::isfinite(value) ? 1 : 0);
      return 1;
    }

    if (!lua_istable(state, kLuaArg1)) {
      lua_pushboolean(state, 0);
      return 1;
    }

    bool has_values = false;
    bool all_finite = true;
    for (int i = 1; i <= kLuaMatElementCount; ++i) {
      lua_rawgeti(state, kLuaArg1, i);
      if (lua_isnumber(state, -1) != 0) {
        has_values = true;
        const float value = static_cast<float>(lua_tonumber(state, -1));
        all_finite = all_finite && std::isfinite(value);
      }
      lua_pop(state, 1);
    }

    for (const char* field_name : { "x", "y", "z", "w" }) {
      float value = 0.0F;
      if (ReadFieldNumber(state, kLuaArg1, field_name, value)) {
        has_values = true;
        all_finite = all_finite && std::isfinite(value);
      }
    }

    lua_pushboolean(state, (has_values && all_finite) ? 1 : 0);
    return 1;
  }

  auto LuaMathNearEqual(lua_State* state) -> int
  {
    const float epsilon = (lua_isnumber(state, kLuaArg3) != 0)
      ? static_cast<float>(lua_tonumber(state, kLuaArg3))
      : kDefaultNearEqualEpsilon;

    if (lua_isnumber(state, kLuaArg1) != 0
      && lua_isnumber(state, kLuaArg2) != 0) {
      const auto lhs = static_cast<float>(lua_tonumber(state, kLuaArg1));
      const auto rhs = static_cast<float>(lua_tonumber(state, kLuaArg2));
      lua_pushboolean(state, std::fabs(lhs - rhs) <= epsilon ? 1 : 0);
      return 1;
    }

    if (!lua_istable(state, kLuaArg1) || !lua_istable(state, kLuaArg2)) {
      lua_pushboolean(state, 0);
      return 1;
    }

    int compared = 0;
    bool all_equal = true;

    for (const char* field_name : { "x", "y", "z", "w" }) {
      auto lhs = 0.0F;
      auto rhs = 0.0F;
      const bool lhs_ok = ReadFieldNumber(state, kLuaArg1, field_name, lhs);
      const bool rhs_ok = ReadFieldNumber(state, kLuaArg2, field_name, rhs);
      if (lhs_ok != rhs_ok) {
        lua_pushboolean(state, 0);
        return 1;
      }
      if (!lhs_ok) {
        continue;
      }
      ++compared;
      all_equal = all_equal && (std::fabs(lhs - rhs) <= epsilon);
    }

    for (int i = 1; i <= kLuaMatElementCount; ++i) {
      lua_rawgeti(state, kLuaArg1, i);
      const bool lhs_ok = lua_isnumber(state, -1) != 0;
      const auto lhs
        = lhs_ok ? static_cast<float>(lua_tonumber(state, -1)) : 0.0F;
      lua_pop(state, 1);

      lua_rawgeti(state, kLuaArg2, i);
      const bool rhs_ok = lua_isnumber(state, -1) != 0;
      const auto rhs
        = rhs_ok ? static_cast<float>(lua_tonumber(state, -1)) : 0.0F;
      lua_pop(state, 1);

      if (lhs_ok != rhs_ok) {
        lua_pushboolean(state, 0);
        return 1;
      }
      if (!lhs_ok) {
        continue;
      }
      ++compared;
      all_equal = all_equal && (std::fabs(lhs - rhs) <= epsilon);
    }

    lua_pushboolean(state, (compared > 0 && all_equal) ? 1 : 0);
    return 1;
  }
} // namespace

auto RegisterMathBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
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
