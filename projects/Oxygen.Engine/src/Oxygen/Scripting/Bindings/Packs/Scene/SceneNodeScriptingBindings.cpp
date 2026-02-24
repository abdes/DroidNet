//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeBindings.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeComponentBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  class ScopedLuaStackTop final {
  public:
    explicit ScopedLuaStackTop(
      lua_State* state, const char* scope_name) noexcept
      : state_(state)
      , top_(state != nullptr ? lua_gettop(state) : 0)
      , scope_name_(scope_name)
    {
    }

    ~ScopedLuaStackTop()
    {
      if (state_ != nullptr) {
        const int current_top = lua_gettop(state_);
        CHECK_F(current_top == top_,
          "lua stack imbalance in {}: entry_top={} exit_top={}", scope_name_,
          top_, current_top);
      }
    }

  private:
    lua_State* state_ { nullptr };
    int top_ { 0 };
    const char* scope_name_ { "unknown_scope" };
  };

  constexpr const char* kSlotRefIndexKey = "index";
  constexpr const char* kSlotRefPtrKey = "__slot_ptr";

  auto PushSlotRef(lua_State* state,
    const scene::ScriptingComponent::Slot& slot, const std::size_t index) -> int
  {
    lua_createtable(state, 0, 2);
    lua_pushinteger(state, static_cast<lua_Integer>(index + 1));
    lua_setfield(state, -2, kSlotRefIndexKey);
    // Pointer guard makes invalidated references fail deterministically.
    lua_pushlightuserdata(state,
      // NOLINTNEXTLINE(*-const-cast)
      const_cast<scene::ScriptingComponent::Slot*>(&slot));
    lua_setfield(state, -2, kSlotRefPtrKey);
    return 1;
  }

  auto ParseSlotRef(
    lua_State* state, const int index, scene::SceneNode::Scripting& scripting)
    -> const scene::ScriptingComponent::Slot*
  {
    const ScopedLuaStackTop stack_guard(state, "ParseSlotRef");
    if (lua_type(state, index) != LUA_TTABLE) {
      return nullptr;
    }

    lua_getfield(state, index, kSlotRefIndexKey);
    if (lua_isnumber(state, -1) == 0) {
      lua_pop(state, 1);
      return nullptr;
    }
    const auto raw_index = lua_tointeger(state, -1);
    lua_pop(state, 1);
    if (raw_index < 1) {
      return nullptr;
    }
    const auto slot_index = static_cast<std::size_t>(raw_index - 1);

    const auto slots = scripting.Slots();
    if (slot_index >= slots.size()) {
      return nullptr;
    }
    const auto* slot = &slots[slot_index];

    lua_getfield(state, index, kSlotRefPtrKey);
    const auto* ptr = static_cast<const scene::ScriptingComponent::Slot*>(
      lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (ptr != slot) {
      return nullptr;
    }
    return slot;
  }

  auto ParseScriptParam(
    lua_State* state, const int index, data::ScriptParam& out) -> bool
  {
    const ScopedLuaStackTop stack_guard(state, "ParseScriptParam");
    const int value_type = lua_type(state, index);
    if (value_type == LUA_TBOOLEAN) {
      out = lua_toboolean(state, index) != 0;
      return true;
    }
    if (value_type == LUA_TNUMBER) {
      const auto number = lua_tonumber(state, index);
      if (std::isfinite(number) && (std::floor(number) == number)
        && (number
          >= static_cast<double>(std::numeric_limits<std::int32_t>::min()))
        && (number
          <= static_cast<double>(std::numeric_limits<std::int32_t>::max()))) {
        out = static_cast<std::int32_t>(number);
        return true;
      }
      out = static_cast<float>(number);
      return true;
    }
    if (value_type == LUA_TSTRING) {
      size_t len = 0;
      const char* text = lua_tolstring(state, index, &len);
      if (text == nullptr) {
        return false;
      }
      out = std::string(text, len);
      return true;
    }
    if (lua_isvector(state, index) != 0) {
      const float* v = lua_tovector(state, index);
      out = Vec3 { v[0], v[1], v[2] }; // NOLINT
      return true;
    }
    if (value_type != LUA_TTABLE) {
      return false;
    }

    const auto len = lua_objlen(state, index);
    if (len == 2) {
      lua_rawgeti(state, index, 1);
      lua_rawgeti(state, index, 2);
      const bool valid
        = (lua_isnumber(state, -2) != 0) && (lua_isnumber(state, -1) != 0);
      if (valid) {
        out = Vec2 { static_cast<float>(lua_tonumber(state, -2)),
          static_cast<float>(lua_tonumber(state, -1)) };
      }
      lua_pop(state, 2);
      return valid;
    }
    if (len == 3) {
      lua_rawgeti(state, index, 1);
      lua_rawgeti(state, index, 2);
      lua_rawgeti(state, index, 3);
      const bool valid = (lua_isnumber(state, -3) != 0)
        && (lua_isnumber(state, -2) != 0) && (lua_isnumber(state, -1) != 0);
      if (valid) {
        out = Vec3 { static_cast<float>(lua_tonumber(state, -3)),
          static_cast<float>(lua_tonumber(state, -2)),
          static_cast<float>(lua_tonumber(state, -1)) };
      }
      lua_pop(state, 3);
      return valid;
    }
    if (len == 4) {
      lua_rawgeti(state, index, 1);
      lua_rawgeti(state, index, 2);
      lua_rawgeti(state, index, 3);
      lua_rawgeti(state, index, 4);
      const bool valid = (lua_isnumber(state, -4) != 0)
        && (lua_isnumber(state, -3) != 0) && (lua_isnumber(state, -2) != 0)
        && (lua_isnumber(state, -1) != 0);
      if (valid) {
        out = Vec4 { static_cast<float>(lua_tonumber(state, -4)),
          static_cast<float>(lua_tonumber(state, -3)),
          static_cast<float>(lua_tonumber(state, -2)),
          static_cast<float>(lua_tonumber(state, -1)) };
      }
      lua_pop(state, 4);
      return valid;
    }
    return false;
  }

  auto SceneNodeScripting(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!node->HasScripting()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushvalue(state, 1);
    return 1;
  }

  auto SceneNodeAttachScripting(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->AttachScripting() ? 1 : 0);
    return 1;
  }

  auto SceneNodeDetachScripting(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->DetachScripting() ? 1 : 0);
    return 1;
  }

  auto SceneNodeHasScripting(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, node->HasScripting() ? 1 : 0);
    return 1;
  }

  auto SceneNodeScriptingSlotsCount(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushinteger(state, 0);
      return 1;
    }
    if (!node->HasScripting()) {
      lua_pushinteger(state, 0);
      return 1;
    }
    const auto slots = node->GetScripting().Slots();
    lua_pushinteger(state, static_cast<lua_Integer>(slots.size()));
    return 1;
  }

  auto SceneNodeScriptingSlots(lua_State* state) -> int
  {
    const int entry_top = lua_gettop(state);
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_newtable(state);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    if (!node->HasScripting()) {
      lua_newtable(state);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    const auto slots = node->GetScripting().Slots();
    lua_createtable(state, static_cast<int>(slots.size()), 0);
    for (std::size_t i = 0; i < slots.size(); ++i) {
      PushSlotRef(state, slots[i], i);
      lua_rawseti(state, -2, static_cast<int>(i + 1));
    }
    CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
    return 1;
  }

  auto SceneNodeScriptingAddSlot(lua_State* state) -> int
  {
    const int entry_top = lua_gettop(state);
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    if (!node->HasScripting()) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }

    size_t len = 0;
    if (lua_type(state, 2) != LUA_TSTRING) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    const char* source = lua_tolstring(state, 2, &len);
    if (source == nullptr) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    data::pak::ScriptAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
    desc.bytecode_resource_index = data::pak::kNoResourceIndex;
    desc.source_resource_index = data::pak::kNoResourceIndex;
    desc.flags = data::pak::ScriptAssetFlags::kAllowExternalSource;

    auto path_span = std::span(desc.external_source_path);
    auto writable = path_span.first(path_span.size() - 1);
    const auto copy_len = std::min(writable.size(), len);
    if (copy_len > 0U) {
      std::memcpy(writable.data(), source, copy_len);
    }
    writable[copy_len] = '\0';

    auto asset = std::make_shared<data::ScriptAsset>(
      data::AssetKey {}, desc, std::vector<data::pak::ScriptParamRecord> {});
    lua_pushboolean(
      state, node->GetScripting().AddSlot(std::move(asset)) ? 1 : 0);
    CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
    return 1;
  }

  auto SceneNodeScriptingRemoveSlot(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    if (!node->HasScripting()) {
      lua_pushboolean(state, 0);
      return 1;
    }
    auto scripting = node->GetScripting();
    const auto* slot = ParseSlotRef(state, 2, scripting);
    if (slot == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    lua_pushboolean(state, scripting.RemoveSlot(*slot) ? 1 : 0);
    return 1;
  }

  auto SceneNodeScriptingSetParam(lua_State* state) -> int
  {
    const int entry_top = lua_gettop(state);
    if (entry_top < 4) {
      lua_pushboolean(state, 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    }
    const auto push_and_check_bool = [state, entry_top](const bool value) {
      lua_pushboolean(state, value ? 1 : 0);
      CHECK_F(lua_gettop(state) == entry_top + 1, "stack imbalance");
      return 1;
    };

    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      return push_and_check_bool(false);
    }
    if (!node->HasScripting()) {
      return push_and_check_bool(false);
    }
    auto scripting = node->GetScripting();
    const auto* slot = ParseSlotRef(state, 2, scripting);
    if (slot == nullptr) {
      return push_and_check_bool(false);
    }

    size_t key_len = 0;
    if (lua_type(state, 3) != LUA_TSTRING) {
      return push_and_check_bool(false);
    }
    const char* key = lua_tolstring(state, 3, &key_len);
    if (key == nullptr) {
      return push_and_check_bool(false);
    }
    data::ScriptParam value {};
    if (!ParseScriptParam(state, 4, value)) {
      return push_and_check_bool(false);
    }
    return push_and_check_bool(scripting.SetParameter(
      *slot, std::string_view(key, key_len), std::move(value)));
  }

  auto SceneNodeScriptingGetParam(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    if (!node->HasScripting()) {
      lua_pushnil(state);
      return 1;
    }
    auto scripting = node->GetScripting();
    const auto* slot = ParseSlotRef(state, 2, scripting);
    if (slot == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    size_t key_len = 0;
    const char* key = lua_tolstring(state, 3, &key_len);
    if (key == nullptr) {
      lua_pushnil(state);
      return 1;
    }
    const auto value
      = scripting.TryGetParameter(*slot, std::string_view(key, key_len));
    if (!value.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    return PushScriptParam(state, value->get());
  }

  auto SceneNodeScriptingParams(lua_State* state) -> int
  {
    auto* node = TryCheckSceneNode(state, 1);
    if (node == nullptr) {
      lua_newtable(state);
      return 1;
    }
    if (!node->HasScripting()) {
      lua_newtable(state);
      return 1;
    }
    auto scripting = node->GetScripting();
    const auto* slot = ParseSlotRef(state, 2, scripting);
    if (slot == nullptr) {
      lua_newtable(state);
      return 1;
    }

    lua_newtable(state);
    for (const auto entry : scripting.Parameters(*slot)) {
      lua_pushlstring(state, entry.key.data(), entry.key.size());
      (void)PushScriptParam(state, entry.value.get());
      lua_settable(state, -3);
    }
    return 1;
  }
} // namespace

auto RegisterSceneNodeScriptingMethods(
  lua_State* state, const int metatable_index) -> void
{
  constexpr auto methods = std::to_array<luaL_Reg>({
    { .name = "scripting", .func = SceneNodeScripting },
    { .name = "attach_scripting", .func = SceneNodeAttachScripting },
    { .name = "detach_scripting", .func = SceneNodeDetachScripting },
    { .name = "has_scripting", .func = SceneNodeHasScripting },
    { .name = "scripting_slots_count", .func = SceneNodeScriptingSlotsCount },
    { .name = "scripting_slots", .func = SceneNodeScriptingSlots },
    { .name = "scripting_add_slot", .func = SceneNodeScriptingAddSlot },
    { .name = "scripting_remove_slot", .func = SceneNodeScriptingRemoveSlot },
    { .name = "scripting_set_param", .func = SceneNodeScriptingSetParam },
    { .name = "scripting_get_param", .func = SceneNodeScriptingGetParam },
    { .name = "scripting_params", .func = SceneNodeScriptingParams },
  });

  for (const auto& reg : methods) {
    lua_pushcclosure(state, reg.func, reg.name, 0);
    lua_setfield(state, metatable_index, reg.name);
  }
}

} // namespace oxygen::scripting::bindings
