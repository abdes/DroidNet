//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <cstring>
#include <functional>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingsCommon.h>

namespace oxygen::scripting::test {

class PhysicsShapeBindingsTest : public ScriptingModuleTest { };

namespace {
  using oxygen::physics::BoxShape;
  using oxygen::physics::CapsuleShape;
  using oxygen::physics::CollisionShape;
  using oxygen::physics::CompoundShape;
  using oxygen::physics::ConeShape;
  using oxygen::physics::ConvexHullShape;
  using oxygen::physics::CylinderShape;
  using oxygen::physics::HeightFieldShape;
  using oxygen::physics::PlaneShape;
  using oxygen::physics::ShapePayloadType;
  using oxygen::physics::SphereShape;
  using oxygen::physics::TriangleMeshShape;
  using oxygen::physics::WorldBoundaryMode;
  using oxygen::physics::WorldBoundaryShape;
  using oxygen::scripting::bindings::ParseCollisionShape;

  auto PushPayload(lua_State* state, const char* payload_type, const char* data)
    -> void
  {
    lua_newtable(state);
    lua_pushstring(state, payload_type);
    lua_setfield(state, -2, "payload_type");
    lua_pushlstring(state, data, std::strlen(data));
    lua_setfield(state, -2, "data");
  }

  auto LuaParseShapeForPCall(lua_State* state) -> int
  {
    (void)ParseCollisionShape(state, 1);
    return 0;
  }
} // namespace

NOLINT_TEST_F(
  PhysicsShapeBindingsTest, ParseCollisionShapeSupportsAllShapeTypes)
{
  lua_State* state = luaL_newstate();
  ASSERT_NE(state, nullptr);

  lua_newtable(state);
  lua_pushstring(state, "sphere");
  lua_setfield(state, -2, "type");
  lua_pushnumber(state, 0.75);
  lua_setfield(state, -2, "radius");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<SphereShape>(shape));
    EXPECT_FLOAT_EQ(std::get<SphereShape>(shape).radius, 0.75F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "box");
  lua_setfield(state, -2, "type");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<BoxShape>(shape));
    const auto& box = std::get<BoxShape>(shape);
    EXPECT_FLOAT_EQ(box.extents.x, 0.5F);
    EXPECT_FLOAT_EQ(box.extents.y, 0.5F);
    EXPECT_FLOAT_EQ(box.extents.z, 0.5F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "capsule");
  lua_setfield(state, -2, "type");
  lua_pushnumber(state, 0.2);
  lua_setfield(state, -2, "radius");
  lua_pushnumber(state, 1.5);
  lua_setfield(state, -2, "half_height");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<CapsuleShape>(shape));
    const auto& capsule = std::get<CapsuleShape>(shape);
    EXPECT_FLOAT_EQ(capsule.radius, 0.2F);
    EXPECT_FLOAT_EQ(capsule.half_height, 1.5F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "cylinder");
  lua_setfield(state, -2, "type");
  lua_pushnumber(state, 0.4);
  lua_setfield(state, -2, "radius");
  lua_pushnumber(state, 1.25);
  lua_setfield(state, -2, "half_height");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<CylinderShape>(shape));
    const auto& cylinder = std::get<CylinderShape>(shape);
    EXPECT_FLOAT_EQ(cylinder.radius, 0.4F);
    EXPECT_FLOAT_EQ(cylinder.half_height, 1.25F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "cone");
  lua_setfield(state, -2, "type");
  lua_pushnumber(state, 0.3);
  lua_setfield(state, -2, "radius");
  lua_pushnumber(state, 0.9);
  lua_setfield(state, -2, "half_height");
  PushPayload(state, "convex", "cone_payload");
  lua_setfield(state, -2, "cooked_payload");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<ConeShape>(shape));
    const auto& cone = std::get<ConeShape>(shape);
    EXPECT_FLOAT_EQ(cone.radius, 0.3F);
    EXPECT_FLOAT_EQ(cone.half_height, 0.9F);
    EXPECT_EQ(cone.cooked_payload.payload_type, ShapePayloadType::kConvex);
    EXPECT_FALSE(cone.cooked_payload.data.empty());
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "convex_hull");
  lua_setfield(state, -2, "type");
  PushPayload(state, "convex", "hull_payload");
  lua_setfield(state, -2, "cooked_payload");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<ConvexHullShape>(shape));
    const auto& hull = std::get<ConvexHullShape>(shape);
    EXPECT_EQ(hull.cooked_payload.payload_type, ShapePayloadType::kConvex);
    EXPECT_FALSE(hull.cooked_payload.data.empty());
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "triangle_mesh");
  lua_setfield(state, -2, "type");
  PushPayload(state, "mesh", "mesh_payload");
  lua_setfield(state, -2, "cooked_payload");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<TriangleMeshShape>(shape));
    const auto& mesh = std::get<TriangleMeshShape>(shape);
    EXPECT_EQ(mesh.cooked_payload.payload_type, ShapePayloadType::kMesh);
    EXPECT_FALSE(mesh.cooked_payload.data.empty());
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "height_field");
  lua_setfield(state, -2, "type");
  PushPayload(state, "height_field", "hf_payload");
  lua_setfield(state, -2, "cooked_payload");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<HeightFieldShape>(shape));
    const auto& hf = std::get<HeightFieldShape>(shape);
    EXPECT_EQ(hf.cooked_payload.payload_type, ShapePayloadType::kHeightField);
    EXPECT_FALSE(hf.cooked_payload.data.empty());
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "plane");
  lua_setfield(state, -2, "type");
  lua_pushnumber(state, 2.5);
  lua_setfield(state, -2, "distance");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<PlaneShape>(shape));
    EXPECT_FLOAT_EQ(std::get<PlaneShape>(shape).distance, 2.5F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "world_boundary");
  lua_setfield(state, -2, "type");
  lua_pushstring(state, "aabb_clamp");
  lua_setfield(state, -2, "boundary_mode");
  lua_pushvector(state, -10.0F, -10.0F, -1.0F);
  lua_setfield(state, -2, "limits_min");
  lua_pushvector(state, 10.0F, 10.0F, 4.0F);
  lua_setfield(state, -2, "limits_max");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<WorldBoundaryShape>(shape));
    const auto& wb = std::get<WorldBoundaryShape>(shape);
    EXPECT_EQ(wb.mode, WorldBoundaryMode::kAabbClamp);
    EXPECT_FLOAT_EQ(wb.limits_min.x, -10.0F);
    EXPECT_FLOAT_EQ(wb.limits_max.z, 4.0F);
  }
  lua_pop(state, 1);

  lua_newtable(state);
  lua_pushstring(state, "compound");
  lua_setfield(state, -2, "type");
  PushPayload(state, "compound", "compound_payload");
  lua_setfield(state, -2, "cooked_payload");
  {
    const CollisionShape shape = ParseCollisionShape(state, -1);
    ASSERT_TRUE(std::holds_alternative<CompoundShape>(shape));
    const auto& compound = std::get<CompoundShape>(shape);
    EXPECT_EQ(
      compound.cooked_payload.payload_type, ShapePayloadType::kCompound);
    EXPECT_FALSE(compound.cooked_payload.data.empty());
  }
  lua_pop(state, 1);

  lua_close(state);
}

NOLINT_TEST_F(
  PhysicsShapeBindingsTest, ParseCollisionShapeRejectsInvalidContracts)
{
  lua_State* state = luaL_newstate();
  ASSERT_NE(state, nullptr);

  auto ExpectParseError = [state](const std::function<void()>& push_shape,
                            const char* expected_substring) {
    lua_pushcfunction(state, LuaParseShapeForPCall, "parse_shape_for_pcall");
    push_shape();
    const int status = lua_pcall(state, 1, 0, 0);
    ASSERT_NE(status, LUA_OK);
    const auto* message = lua_tostring(state, -1);
    ASSERT_NE(message, nullptr);
    EXPECT_NE(std::string_view(message).find(expected_substring),
      std::string_view::npos);
    lua_pop(state, 1);
  };

  ExpectParseError(
    [state]() {
      lua_newtable(state);
      lua_pushstring(state, "triangle_mesh");
      lua_setfield(state, -2, "type");
      PushPayload(state, "convex", "wrong_payload");
      lua_setfield(state, -2, "cooked_payload");
    },
    "payload_type does not match");

  ExpectParseError(
    [state]() {
      lua_newtable(state);
      lua_pushstring(state, "compound");
      lua_setfield(state, -2, "type");
      PushPayload(state, "compound", "");
      lua_setfield(state, -2, "cooked_payload");
    },
    "data must be non-empty");

  ExpectParseError(
    [state]() {
      lua_newtable(state);
      lua_pushstring(state, "not_a_shape");
      lua_setfield(state, -2, "type");
    },
    "unsupported shape.type");

  lua_close(state);
}

} // namespace oxygen::scripting::test
