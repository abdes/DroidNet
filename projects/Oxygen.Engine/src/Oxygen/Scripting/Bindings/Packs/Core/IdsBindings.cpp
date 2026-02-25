//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/IdsBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArg1 = 1;

  constexpr const char* kUuidMetatableName = "oxygen.uuid";
  constexpr const char* kHashMetatableName = "oxygen.hash";

  using UuidBytes = decltype(data::GenerateAssetGuid());
  constexpr size_t kUuidByteCount = std::tuple_size_v<UuidBytes>;
  constexpr size_t kUuidStringLength = 36;
  constexpr unsigned kHexMask = 0xF;
  constexpr size_t kDashPosA = 4;
  constexpr size_t kDashPosB = 6;
  constexpr size_t kDashPosC = 8;
  constexpr size_t kDashPosD = 10;
  constexpr size_t kDashIndex0 = 8;
  constexpr size_t kDashIndex1 = 13;
  constexpr size_t kDashIndex2 = 18;
  constexpr size_t kDashIndex3 = 23;

  // Hex lookup optimization could go here, but std::from_chars is C++17.
  // We'll stick to a robust manual parser or optimized version.

  struct UuidUserdata {
    UuidBytes bytes;
  };

  struct HashUserdata {
    std::uint64_t value;
  };

  // --- Helper Functions ---

  auto ToUuid(lua_State* state, int index) -> UuidUserdata*
  {
    void* data = lua_touserdata(state, index);
    if (data != nullptr) {
      if (lua_getmetatable(state, index) != 0) {
        lua_getfield(state, LUA_REGISTRYINDEX, kUuidMetatableName);
        if (lua_rawequal(state, -1, -2) != 0) {
          lua_pop(state, 2);
          return static_cast<UuidUserdata*>(data);
        }
        lua_pop(state, 2);
      }
    }
    return nullptr;
  }

  auto BytesToUuidString(const UuidBytes& bytes) -> std::string
  {
    // Optimized string formatting
    // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    static constexpr std::string_view kHexChars { "0123456789abcdef" };
    std::string out;
    out.resize(kUuidStringLength);

    size_t out_idx = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
      if (i == kDashPosA || i == kDashPosB || i == kDashPosC
        || i == kDashPosD) {
        out[out_idx++] = '-';
      }
      uint8_t b = bytes.at(i);
      out[out_idx++] = kHexChars[static_cast<size_t>((b >> 4) & kHexMask)];
      out[out_idx++] = kHexChars[static_cast<size_t>(b & kHexMask)];
    }
    return out;
  }

  auto ParseUuidString(std::string_view text, UuidBytes& out) -> bool
  {
    if (text.size() != kUuidStringLength) {
      return false;
    }

    // Validate dashes
    if (text[kDashIndex0] != '-' || text[kDashIndex1] != '-'
      || text[kDashIndex2] != '-' || text[kDashIndex3] != '-') {
      return false;
    }

    auto hex_val = [](char c) -> int {
      constexpr int kBaseTen = 10;
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'a' && c <= 'f') {
        return kBaseTen + (c - 'a');
      }
      if (c >= 'A' && c <= 'F') {
        return kBaseTen + (c - 'A');
      }
      return -1;
    };

    size_t byte_idx = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '-') {
        continue;
      }

      int hi = hex_val(text[i]);
      int lo = hex_val(text[++i]);

      if (hi < 0 || lo < 0) {
        return false;
      }

      out.at(byte_idx++) = static_cast<uint8_t>((hi << 4) | lo);
    }
    return byte_idx == kUuidByteCount;
  }

  auto PushUuid(lua_State* state, const UuidBytes& bytes) -> void
  {
    auto* data = static_cast<UuidUserdata*>(
      lua_newuserdata(state, sizeof(UuidUserdata)));
    data->bytes = bytes;
    luaL_getmetatable(state, kUuidMetatableName);
    lua_setmetatable(state, -2);
  }

  auto PushHash(lua_State* state, std::uint64_t value) -> void
  {
    auto* data = static_cast<HashUserdata*>(
      lua_newuserdata(state, sizeof(HashUserdata)));
    data->value = value;
    luaL_getmetatable(state, kHashMetatableName);
    lua_setmetatable(state, -2);
  }

  // --- UUID Bindings ---

  auto LuaUuidNew(lua_State* state) -> int
  {
    PushUuid(state, data::GenerateAssetGuid());
    return 1;
  }

  auto LuaUuidToString(lua_State* state) -> int
  {
    if (auto* u = ToUuid(state, kLuaArg1); u != nullptr) {
      std::string s = BytesToUuidString(u->bytes);
      lua_pushlstring(state, s.c_str(), s.size());
      return 1;
    }
    luaL_error(state, "uuid.to_string expects uuid userdata");
    return 0;
  }

  auto LuaUuidFromString(lua_State* state) -> int
  {
    if (lua_type(state, kLuaArg1) != LUA_TSTRING) {
      luaL_argerror(state, kLuaArg1, "uuid string expected");
      return 0;
    }
    size_t len = 0;
    const char* str = lua_tolstring(state, kLuaArg1, &len);
    if (str == nullptr) {
      luaL_argerror(state, kLuaArg1, "uuid string expected");
      return 0;
    }

    UuidBytes bytes;
    if (ParseUuidString(std::string_view(str, len), bytes)) {
      PushUuid(state, bytes);
      return 1;
    }

    lua_pushnil(state);
    return 1;
  }

  auto LuaUuidIsValid(lua_State* state) -> int
  {
    if (ToUuid(state, kLuaArg1) != nullptr) {
      lua_pushboolean(state, 1);
      return 1;
    }
    size_t len = 0;
    const char* str = lua_tolstring(state, kLuaArg1, &len);
    if (str != nullptr) {
      UuidBytes bytes;
      lua_pushboolean(
        state, ParseUuidString(std::string_view(str, len), bytes) ? 1 : 0);
      return 1;
    }
    lua_pushboolean(state, 0);
    return 1;
  }

  auto LuaUuidEq(lua_State* state) -> int
  {
    auto* a = ToUuid(state, 1);
    auto* b = ToUuid(state, 2);
    if (a != nullptr && b != nullptr) {
      lua_pushboolean(state, (a->bytes == b->bytes) ? 1 : 0);
    } else {
      lua_pushboolean(state, 0);
    }
    return 1;
  }

  // --- Hash Bindings ---

  auto CheckHash(lua_State* state, int index) -> std::uint64_t
  {
    void* data = lua_touserdata(state, index);
    if (data != nullptr) {
      if (lua_getmetatable(state, index) != 0) {
        lua_getfield(state, LUA_REGISTRYINDEX, kHashMetatableName);
        if (lua_rawequal(state, -1, -2) != 0) {
          lua_pop(state, 2);
          return static_cast<HashUserdata*>(data)->value;
        }
        lua_pop(state, 2);
      }
    }
    if (lua_isnumber(state, index) != 0) {
      const lua_Number number_value = lua_tonumber(state, index);
      const lua_Integer integer_value = lua_tointeger(state, index);
      if (static_cast<lua_Number>(integer_value) == number_value) {
        return static_cast<std::uint64_t>(integer_value);
      }
      luaL_argerror(
        state, index, "hash integer, hash userdata, or string expected");
      return 0;
    }
    if (lua_isstring(state, index) != 0) {
      size_t len = 0;
      const char* s = lua_tolstring(state, index, &len);
      if (s != nullptr) {
        return ComputeFNV1a64(s, len);
      }
    }
    luaL_argerror(state, index, "hash, number, or string expected");
    return 0;
  }

  auto LuaHash64(lua_State* state) -> int
  {
    if (lua_type(state, kLuaArg1) != LUA_TSTRING) {
      luaL_argerror(state, kLuaArg1, "string expected");
      return 0;
    }
    size_t len = 0;
    const char* s = lua_tolstring(state, kLuaArg1, &len);
    if (s != nullptr) {
      PushHash(state, ComputeFNV1a64(s, len));
      return 1;
    }
    luaL_argerror(state, kLuaArg1, "string expected");
    return 0;
  }

  auto LuaHashCombine64(lua_State* state) -> int
  {
    std::uint64_t seed = CheckHash(state, 1);
    std::uint64_t value = CheckHash(state, 2);

    HashCombine(seed, value);

    PushHash(state, seed);
    return 1;
  }

  auto LuaHashToString(lua_State* state) -> int
  {
    // For debugging
    uint64_t val = CheckHash(state, 1);
    std::string s = std::to_string(val);
    lua_pushlstring(state, s.c_str(), s.size());
    return 1;
  }

  auto LuaHashEq(lua_State* state) -> int
  {
    uint64_t a = CheckHash(state, 1);
    uint64_t b = CheckHash(state, 2);
    lua_pushboolean(state, (a == b) ? 1 : 0);
    return 1;
  }

} // namespace

auto RegisterUuidBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  // Register Metatable
  if (luaL_newmetatable(state, kUuidMetatableName) != 0) {
    lua_pushcfunction(state, LuaUuidToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, LuaUuidEq, "__eq");
    lua_setfield(state, -2, "__eq");
  }
  lua_pop(state, 1);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "uuid");

  lua_pushcfunction(state, LuaUuidNew, "uuid.new");
  lua_setfield(state, module_index, "new");
  lua_pushcfunction(state, LuaUuidIsValid, "uuid.is_valid");
  lua_setfield(state, module_index, "is_valid");
  lua_pushcfunction(state, LuaUuidToString, "uuid.to_string");
  lua_setfield(state, module_index, "to_string");
  lua_pushcfunction(state, LuaUuidFromString, "uuid.from_string");
  lua_setfield(state, module_index, "from_string");

  lua_pop(state, 1);
}

auto RegisterHashBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
  if (luaL_newmetatable(state, kHashMetatableName) != 0) {
    lua_pushcfunction(state, LuaHashToString, "__tostring");
    lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, LuaHashEq, "__eq");
    lua_setfield(state, -2, "__eq");
  }
  lua_pop(state, 1);

  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "hash");

  lua_pushcfunction(state, LuaHash64, "hash.hash64");
  lua_setfield(state, module_index, "hash64");
  lua_pushcfunction(state, LuaHashCombine64, "hash.combine64");
  lua_setfield(state, module_index, "combine64");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
