//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class MathBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(MathBindingsTest, ExecuteScriptMathBindingsSupportEngineTypes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local m = oxygen.math
local eps = 0.0001

local d = m.deg_to_rad(180.0)
if not m.near_equal(d, 3.14159265, eps) then
  error("deg_to_rad failed")
end

local q = m.quat_from_euler_xyz(0.0, 0.0, 0.0)
local e = m.euler_xyz_from_quat(q)
if not m.near_equal(e.x, 0.0, eps) or not m.near_equal(e.y, 0.0, eps)
   or not m.near_equal(e.z, 0.0, eps) then
  error("quat/euler conversion failed")
end

local v = m.vec3(1.0, 2.0, 3.0)
local vr = m.rotate_vec3(q, v)
if not m.near_equal(v.x, vr.x, eps) or not m.near_equal(v.y, vr.y, eps)
   or not m.near_equal(v.z, vr.z, eps) then
  error("rotate_vec3 failed")
end

local t = m.vec3(1.0, 2.0, 3.0)
local s = m.vec3(1.0, 1.0, 1.0)
local mat = m.mat4_trs(t, m.quat(0.0, 0.0, 0.0, 1.0), s)
if not m.is_finite(mat) then
  error("mat4_trs is not finite")
end

local id = m.mat4_identity()
local combined = m.mat4_mul(id, mat)
if not m.near_equal(combined, mat, eps) then
  error("mat4_mul failed")
end

local p = m.vec3(0.0, 0.0, 0.0)
local pt = m.mat4_transform_point(mat, p)
if not m.near_equal(pt.x, 1.0, eps) or not m.near_equal(pt.y, 2.0, eps)
   or not m.near_equal(pt.z, 3.0, eps) then
  error("mat4_transform_point failed")
end

local d0 = m.vec3(0.0, 0.0, -1.0)
local dt = m.mat4_transform_direction(mat, d0)
if not m.near_equal(dt.x, 0.0, eps) or not m.near_equal(dt.y, 0.0, eps)
   or not m.near_equal(dt.z, -1.0, eps) then
  error("mat4_transform_direction failed")
end

local view = m.mat4_look_at_rh(m.vec3(0.0, -5.0, 0.0), m.vec3(0.0, 0.0, 0.0))
if not m.is_finite(view) then
  error("mat4_look_at_rh failed")
end
)lua" },
    .chunk_name = ScriptChunkName { "math_bindings" },
  });

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(MathBindingsTest, ConventionsVectorsUseNativeLuauVectorType)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local c = oxygen.conventions
if type(c.world.up) ~= "vector" then
  error("world.up must be Luau vector")
end
if type(c.world.forward) ~= "vector" then
  error("world.forward must be Luau vector")
end
if type(c.world.right) ~= "vector" then
  error("world.right must be Luau vector")
end
if type(c.view.up) ~= "vector" then
  error("view.up must be Luau vector")
end
if type(c.view.forward) ~= "vector" then
  error("view.forward must be Luau vector")
end
if type(c.view.right) ~= "vector" then
  error("view.right must be Luau vector")
end
)lua" },
    .chunk_name = ScriptChunkName { "conventions_vectors" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(MathBindingsTest, MathBindingsRejectInvalidArgumentShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local m = oxygen.math

local q = m.quat(0.0, 0.0, 0.0, 1.0)
local id = m.mat4_identity()

local ok = pcall(function() m.quat_mul(q, 42) end)
if ok then error("quat_mul should reject non-quat/non-vector") end

ok = pcall(function() m.rotate_vec3(q, 42) end)
if ok then error("rotate_vec3 should reject non-vector") end

ok = pcall(function() m.mat4_transform_point(id, 42) end)
if ok then error("mat4_transform_point should reject non-vector") end

ok = pcall(function() m.mat4_transform_direction(id, 42) end)
if ok then error("mat4_transform_direction should reject non-vector") end

ok = pcall(function() m.mat4_look_at_rh(42, m.vec3(0.0, 0.0, 0.0)) end)
if ok then error("mat4_look_at_rh should reject invalid eye") end

ok = pcall(function() m.mat4_look_at_rh(m.vec3(0.0, 0.0, 0.0), 42) end)
if ok then error("mat4_look_at_rh should reject invalid target") end

ok = pcall(function() m.mat4_look_at_rh(
  m.vec3(0.0, 0.0, 0.0),
  m.vec3(0.0, 1.0, 0.0),
  42) end)
if ok then error("mat4_look_at_rh should reject invalid up") end

ok = pcall(function() m.mat4_trs(42, q, m.vec3(1.0, 1.0, 1.0)) end)
if ok then error("mat4_trs should reject invalid translation") end

ok = pcall(function() m.mat4_trs(m.vec3(0.0, 0.0, 0.0), q, 42) end)
if ok then error("mat4_trs should reject invalid scale") end

ok = pcall(function() m.is_finite("bad") end)
if ok then error("is_finite should reject unsupported types") end

ok = pcall(function() m.near_equal("a", "b") end)
if ok then error("near_equal should reject unsupported types") end
)lua" },
    .chunk_name = ScriptChunkName { "math_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(MathBindingsTest, ExecuteScriptMathBindingsScalars)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local m = oxygen.math
local eps = 0.0001

if not m.near_equal(m.rad_to_deg(m.deg_to_rad(180.0)), 180.0, eps) then
  error("rad_to_deg / deg_to_rad roundtrip failed")
end

if m.clamp01(-5.0) ~= 0.0 or m.clamp01(5.0) ~= 1.0 or m.clamp01(0.5) ~= 0.5 then
  error("clamp01 failed")
end

-- Use 450 degrees (2.5 pi) to avoid atan2 jumping between +pi and -pi due to float precision
local pi_half = m.deg_to_rad(90.0)
if not m.near_equal(m.normalize_angle_rad(5.0 * pi_half), pi_half, eps) then
  error("normalize_angle_rad failed")
end

if not m.near_equal(m.normalize_angle_deg(450.0), 90.0, eps) then
  error("normalize_angle_deg failed")
end
)lua" },
    .chunk_name = ScriptChunkName { "math_scalars" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(MathBindingsTest, ExecuteScriptMathBindingsVectorsAndQuats)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local m = oxygen.math
local eps = 0.0001

-- vec2
local v2 = m.vec2(1.0, 2.0)
if type(v2) ~= "vector" or v2.x ~= 1.0 or v2.y ~= 2.0 or v2.z ~= 0.0 then
  error("vec2 failed")
end

-- vec4 properties & tostring
local v4 = m.vec4(1.0, 2.0, 3.0, 4.0)
if v4.x ~= 1.0 or v4.y ~= 2.0 or v4.z ~= 3.0 or v4.w ~= 4.0 or v4.invalid ~= nil then
  error("vec4 index failed")
end
if not string.match(tostring(v4), "Vec4") then error("vec4 tostring failed") end

-- quat properties & tostring
local q1 = m.quat(0.0, 0.0, 0.0, 1.0)
if q1.x ~= 0.0 or q1.y ~= 0.0 or q1.z ~= 0.0 or q1.w ~= 1.0 or q1.invalid ~= nil then
  error("quat index failed")
end
if not string.match(tostring(q1), "Quat") then error("quat tostring failed") end

-- quat_normalize
local qn = m.quat_normalize(m.quat(0.0, 2.0, 0.0, 0.0))
if not m.near_equal(qn.y, 1.0, eps) then error("quat_normalize failed") end

-- mat4 tostring
local mat = m.mat4_identity()
if not string.match(tostring(mat), "Mat4") then error("mat4 tostring failed") end

-- near_equal overloads
if not m.near_equal(v4, m.vec4(1.0, 2.0, 3.0, 4.0)) then error("near_equal vec4 failed") end
if not m.near_equal(q1, m.quat(0.0, 0.0, 0.0, 1.0)) then error("near_equal quat failed") end
if not m.near_equal(mat, m.mat4_identity()) then error("near_equal mat4 failed") end
)lua" },
    .chunk_name = ScriptChunkName { "math_vectors_quats" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(MathBindingsTest, ExecuteScriptMathBindingsMetamethods)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local m = oxygen.math
local eps = 0.0001

local q1 = m.quat_from_euler_xyz(0.0, 1.570796, 0.0)
local q2 = m.quat_from_euler_xyz(1.570796, 0.0, 0.0)

-- quat * quat metamethod
local q3 = q1 * q2
local q3_fn = m.quat_mul(q1, q2)
if not m.near_equal(q3, q3_fn, eps) then error("quat __mul quat failed") end

-- quat * vector metamethod
local v = m.vec3(1.0, 0.0, 0.0)
local vr1 = q1 * v
local vr2 = m.rotate_vec3(q1, v)
if not m.near_equal(vr1.x, vr2.x, eps) or not m.near_equal(vr1.y, vr2.y, eps) then
  error("quat __mul vec3 failed")
end

-- quat __unm metamethod
local q_neg = -q1
if not m.near_equal(q_neg.y, -q1.y, eps) then error("quat __unm failed") end

-- mat4 * mat4 metamethod
local m1 = m.mat4_trs(m.vec3(1.0, 0.0, 0.0), m.quat(0.0, 0.0, 0.0, 1.0), m.vec3(1.0, 1.0, 1.0))
local m2 = m.mat4_trs(m.vec3(0.0, 1.0, 0.0), m.quat(0.0, 0.0, 0.0, 1.0), m.vec3(1.0, 1.0, 1.0))
local m3 = m1 * m2
local m3_fn = m.mat4_mul(m1, m2)
if not m.near_equal(m3, m3_fn, eps) then error("mat4 __mul failed") end
)lua" },
    .chunk_name = ScriptChunkName { "math_metamethods" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
