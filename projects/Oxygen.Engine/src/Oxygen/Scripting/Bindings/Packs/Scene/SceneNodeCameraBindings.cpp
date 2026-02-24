//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
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

  auto PushViewport(lua_State* state, const ViewPort& vp) -> int
  {
    lua_createtable(state, 0, 6); // NOLINT(*-magic-numbers)
    lua_pushnumber(state, vp.top_left_x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, vp.top_left_y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, vp.width);
    lua_setfield(state, -2, "width");
    lua_pushnumber(state, vp.height);
    lua_setfield(state, -2, "height");
    lua_pushnumber(state, vp.min_depth);
    lua_setfield(state, -2, "min_depth");
    lua_pushnumber(state, vp.max_depth);
    lua_setfield(state, -2, "max_depth");
    return 1;
  }

  auto TryReadViewport(lua_State* state, const int table_index, ViewPort& out)
    -> bool
  {
    float v = 0.0F;
    if (!TryGetNumberField(state, table_index, "x", v)) {
      return false;
    }
    out.top_left_x = v;
    if (!TryGetNumberField(state, table_index, "y", v)) {
      return false;
    }
    out.top_left_y = v;
    if (!TryGetNumberField(state, table_index, "width", v)) {
      return false;
    }
    out.width = v;
    if (!TryGetNumberField(state, table_index, "height", v)) {
      return false;
    }
    out.height = v;
    out.min_depth = 0.0F;
    out.max_depth = 1.0F;
    (void)TryGetNumberField(state, table_index, "min_depth", out.min_depth);
    (void)TryGetNumberField(state, table_index, "max_depth", out.max_depth);
    return out.IsValid();
  }

  auto PushExposure(lua_State* state, const scene::CameraExposure& e) -> int
  {
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, e.aperture_f);
    lua_setfield(state, -2, "aperture_f");
    lua_pushnumber(state, e.shutter_rate);
    lua_setfield(state, -2, "shutter_rate");
    lua_pushnumber(state, e.iso);
    lua_setfield(state, -2, "iso");
    lua_pushnumber(state, e.GetEv());
    lua_setfield(state, -2, "ev");
    return 1;
  }

  auto TryReadExposure(
    lua_State* state, const int table_index, scene::CameraExposure& out) -> bool
  {
    bool any = false;
    float v = 0.0F;
    if (TryGetNumberField(state, table_index, "aperture_f", v)) {
      out.aperture_f = v;
      any = true;
    }
    if (TryGetNumberField(state, table_index, "shutter_rate", v)) {
      out.shutter_rate = v;
      any = true;
    }
    if (TryGetNumberField(state, table_index, "iso", v)) {
      out.iso = v;
      any = true;
    }
    return any;
  }

  auto SceneNodeCamera(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!node->HasCamera()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeAttachPerspectiveCamera(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto camera = std::make_unique<scene::PerspectiveCamera>();
    if (lua_istable(state, 2) != 0) {
      float value = 0.0F;
      if (TryGetNumberField(state, 2, "fov_y", value)) {
        camera->SetFieldOfView(value);
      }
      if (TryGetNumberField(state, 2, "aspect", value)) {
        camera->SetAspectRatio(value);
      }
      if (TryGetNumberField(state, 2, "near_plane", value)) {
        camera->SetNearPlane(value);
      }
      if (TryGetNumberField(state, 2, "far_plane", value)) {
        camera->SetFarPlane(value);
      }
    }
    lua_pushboolean(state, node->AttachCamera(std::move(camera)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeAttachOrthographicCamera(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto camera = std::make_unique<scene::OrthographicCamera>();
    if (lua_istable(state, 2) != 0) {
      float left = 0.0F;
      float right = 0.0F;
      float bottom = 0.0F;
      float top = 0.0F;
      float near_plane = 0.1F; // NOLINT(*-magic-numbers)
      float far_plane = 1000.0F; // NOLINT(*-magic-numbers)
      const bool has_extents = TryGetNumberField(state, 2, "left", left)
        && TryGetNumberField(state, 2, "right", right)
        && TryGetNumberField(state, 2, "bottom", bottom)
        && TryGetNumberField(state, 2, "top", top);
      (void)TryGetNumberField(state, 2, "near_plane", near_plane);
      (void)TryGetNumberField(state, 2, "far_plane", far_plane);
      if (has_extents) {
        camera->SetExtents(left, right, bottom, top, near_plane, far_plane);
      }
    }
    lua_pushboolean(state, node->AttachCamera(std::move(camera)) ? 1 : 0);
    return 1;
  }

  auto SceneNodeDetachCamera(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->DetachCamera() ? 1 : 0);
    return 1;
  }

  auto SceneNodeHasCamera(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->HasCamera() ? 1 : 0);
    return 1;
  }

  auto SceneNodeCameraType(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (node->GetCameraAs<scene::PerspectiveCamera>().has_value()) {
      lua_pushliteral(state, "perspective");
      return 1;
    }
    if (node->GetCameraAs<scene::OrthographicCamera>().has_value()) {
      lua_pushliteral(state, "orthographic");
      return 1;
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeCameraGetPerspective(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
    if (!cam.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, cam->get().GetFieldOfView());
    lua_setfield(state, -2, "fov_y");
    lua_pushnumber(state, cam->get().GetAspectRatio());
    lua_setfield(state, -2, "aspect");
    lua_pushnumber(state, cam->get().GetNearPlane());
    lua_setfield(state, -2, "near_plane");
    lua_pushnumber(state, cam->get().GetFarPlane());
    lua_setfield(state, -2, "far_plane");
    return 1;
  }

  auto SceneNodeCameraSetPerspective(lua_State* state) -> int
  {
    const int entry_top = lua_gettop(state);
    if (entry_top < 2) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
    if (!cam.has_value()) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    float value = 0.0F;
    if (TryGetNumberField(state, 2, "fov_y", value)) {
      cam->get().SetFieldOfView(value);
    }
    if (TryGetNumberField(state, 2, "aspect", value)) {
      cam->get().SetAspectRatio(value);
    }
    if (TryGetNumberField(state, 2, "near_plane", value)) {
      cam->get().SetNearPlane(value);
    }
    if (TryGetNumberField(state, 2, "far_plane", value)) {
      cam->get().SetFarPlane(value);
    }
    lua_pushboolean(state, 1);
    CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
    return 1;
  }

  auto SceneNodeCameraGetOrthographic(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto cam = node->GetCameraAs<scene::OrthographicCamera>();
    if (!cam.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    const auto e = cam->get().GetExtents();
    lua_createtable(state, 0, 6); // NOLINT(*-magic-numbers)
    lua_pushnumber(state, e[0]);
    lua_setfield(state, -2, "left");
    lua_pushnumber(state, e[1]);
    lua_setfield(state, -2, "right");
    lua_pushnumber(state, e[2]);
    lua_setfield(state, -2, "bottom");
    lua_pushnumber(state, e[3]);
    lua_setfield(state, -2, "top");
    lua_pushnumber(state, e[4]);
    lua_setfield(state, -2, "near_plane");
    lua_pushnumber(state, e[5]); // NOLINT(*-magic-numbers)
    lua_setfield(state, -2, "far_plane");
    return 1;
  }

  auto SceneNodeCameraSetOrthographic(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const auto cam = node->GetCameraAs<scene::OrthographicCamera>();
    if (!cam.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    float left = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float top = 0.0F;
    float near_plane = 0.1F; // NOLINT(*-magic-numbers)
    float far_plane = 1000.0F; // NOLINT(*-magic-numbers)
    if (!(TryGetNumberField(state, 2, "left", left)
          && TryGetNumberField(state, 2, "right", right)
          && TryGetNumberField(state, 2, "bottom", bottom)
          && TryGetNumberField(state, 2, "top", top))) {
      lua_pushboolean(state, 0);
      return 1;
    }
    (void)TryGetNumberField(state, 2, "near_plane", near_plane);
    (void)TryGetNumberField(state, 2, "far_plane", far_plane);
    cam->get().SetExtents(left, right, bottom, top, near_plane, far_plane);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeCameraGetViewport(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (const auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
      cam.has_value()) {
      const auto vp = cam->get().GetViewport();
      if (!vp.has_value()) {
        lua_pushnil(state);
        return 1;
      }
      return PushViewport(state, *vp);
    }
    if (const auto cam = node->GetCameraAs<scene::OrthographicCamera>();
      cam.has_value()) {
      const auto vp = cam->get().GetViewport();
      if (!vp.has_value()) {
        lua_pushnil(state);
        return 1;
      }
      return PushViewport(state, *vp);
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeCameraSetViewport(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_isnil(state, 2) != 0) {
      if (auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
        cam.has_value()) {
        cam->get().ResetViewport();
        lua_pushboolean(state, 1);
        return 1;
      }
      if (auto cam = node->GetCameraAs<scene::OrthographicCamera>();
        cam.has_value()) {
        cam->get().ResetViewport();
        lua_pushboolean(state, 1);
        return 1;
      }
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    ViewPort vp {};
    if (!TryReadViewport(state, 2, vp)) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
      cam.has_value()) {
      cam->get().SetViewport(vp);
      lua_pushboolean(state, 1);
      return 1;
    }
    if (auto cam = node->GetCameraAs<scene::OrthographicCamera>();
      cam.has_value()) {
      cam->get().SetViewport(vp);
      lua_pushboolean(state, 1);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  auto SceneNodeCameraGetExposure(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (const auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
      cam.has_value()) {
      return PushExposure(state, cam->get().Exposure());
    }
    if (const auto cam = node->GetCameraAs<scene::OrthographicCamera>();
      cam.has_value()) {
      return PushExposure(state, cam->get().Exposure());
    }
    lua_pushnil(state);
    return 1;
  }

  auto SceneNodeCameraSetExposure(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_type(state, 2) != LUA_TTABLE) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
      cam.has_value()) {
      auto exposure = cam->get().Exposure();
      if (!TryReadExposure(state, 2, exposure)) {
        lua_pushboolean(state, 0);
        return 1;
      }
      cam->get().SetExposure(exposure);
      lua_pushboolean(state, 1);
      return 1;
    }
    if (auto cam = node->GetCameraAs<scene::OrthographicCamera>();
      cam.has_value()) {
      auto exposure = cam->get().Exposure();
      if (!TryReadExposure(state, 2, exposure)) {
        lua_pushboolean(state, 0);
        return 1;
      }
      cam->get().SetExposure(exposure);
      lua_pushboolean(state, 1);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  template <auto Getter> auto CameraGetPerspectiveFloat(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
    if (!cam.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushnumber(state, (cam->get().*Getter)());
    return 1;
  }

  template <auto Setter> auto CameraSetPerspectiveFloat(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto cam = node->GetCameraAs<scene::PerspectiveCamera>();
    if (!cam.has_value()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (lua_isnumber(state, 2) == 0) {
      lua_pushboolean(state, 0);
      return 1;
    }
    const float v = static_cast<float>(lua_tonumber(state, 2));
    (cam->get().*Setter)(v);
    lua_pushboolean(state, 1);
    return 1;
  }

  auto SceneNodeCameraGetExtents(lua_State* state) -> int
  {
    return SceneNodeCameraGetOrthographic(state);
  }

  auto SceneNodeCameraSetExtents(lua_State* state) -> int
  {
    return SceneNodeCameraSetOrthographic(state);
  }
} // namespace

auto RegisterSceneNodeCameraMethods(lua_State* state, const int metatable_index)
  -> void
{
  using spc = scene::PerspectiveCamera;
  constexpr auto methods = std::to_array<luaL_Reg>({
    { .name = "camera", .func = SceneNodeCamera },
    { .name = "attach_perspective_camera",
      .func = SceneNodeAttachPerspectiveCamera },
    { .name = "attach_orthographic_camera",
      .func = SceneNodeAttachOrthographicCamera },
    { .name = "detach_camera", .func = SceneNodeDetachCamera },
    { .name = "has_camera", .func = SceneNodeHasCamera },
    { .name = "camera_type", .func = SceneNodeCameraType },
    { .name = "camera_get_perspective", .func = SceneNodeCameraGetPerspective },
    { .name = "camera_set_perspective", .func = SceneNodeCameraSetPerspective },
    { .name = "camera_get_orthographic",
      .func = SceneNodeCameraGetOrthographic },
    { .name = "camera_set_orthographic",
      .func = SceneNodeCameraSetOrthographic },
    { .name = "camera_get_viewport", .func = SceneNodeCameraGetViewport },
    { .name = "camera_set_viewport", .func = SceneNodeCameraSetViewport },
    { .name = "camera_get_exposure", .func = SceneNodeCameraGetExposure },
    { .name = "camera_set_exposure", .func = SceneNodeCameraSetExposure },
    { .name = "camera_get_fov_y_radians",
      .func = CameraGetPerspectiveFloat<&spc::GetFieldOfView> },
    { .name = "camera_set_fov_y_radians",
      .func = CameraSetPerspectiveFloat<&spc::SetFieldOfView> },
    { .name = "camera_get_aspect_ratio",
      .func = CameraGetPerspectiveFloat<&spc::GetAspectRatio> },
    { .name = "camera_set_aspect_ratio",
      .func = CameraSetPerspectiveFloat<&spc::SetAspectRatio> },
    { .name = "camera_get_near_plane",
      .func = CameraGetPerspectiveFloat<&spc::GetNearPlane> },
    { .name = "camera_set_near_plane",
      .func = CameraSetPerspectiveFloat<&spc::SetNearPlane> },
    { .name = "camera_get_far_plane",
      .func = CameraGetPerspectiveFloat<&spc::GetFarPlane> },
    { .name = "camera_set_far_plane",
      .func = CameraSetPerspectiveFloat<&spc::SetFarPlane> },
    { .name = "camera_get_extents", .func = SceneNodeCameraGetExtents },
    { .name = "camera_set_extents", .func = SceneNodeCameraSetExtents },
  });

  for (const auto& reg : methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, metatable_index, reg.name);
  }
}

} // namespace oxygen::scripting::bindings
