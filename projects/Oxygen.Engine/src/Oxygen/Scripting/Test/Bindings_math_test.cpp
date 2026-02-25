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
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

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

} // namespace oxygen::scripting::test
