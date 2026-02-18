//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
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
  constexpr int kLuaArg2 = 2;
  using UuidBytes = decltype(data::GenerateAssetGuid());
  constexpr size_t kUuidByteCount = std::tuple_size_v<UuidBytes>;
  constexpr size_t kUuidDashCount = 4;
  constexpr size_t kUuidStringLength = (kUuidByteCount * 2) + kUuidDashCount;
  constexpr uint8_t kHexNibbleShift = 4;
  constexpr uint8_t kHexNibbleMask
    = static_cast<uint8_t>((1U << kHexNibbleShift) - 1U);
  constexpr auto kHexDigits = std::to_array<char>({
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    'a',
    'b',
    'c',
    'd',
    'e',
    'f',
  });

  auto IsHexChar(const char c) -> bool
  {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
      || (c >= 'A' && c <= 'F');
  }

  auto HexValue(const char c) -> uint8_t
  {
    constexpr uint8_t kHexAlphabetOffset = 10;
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(kHexAlphabetOffset + (c - 'a'));
    }
    return static_cast<uint8_t>(kHexAlphabetOffset + (c - 'A'));
  }

  auto ParseUuidString(std::string_view text, UuidBytes& out) -> bool
  {
    constexpr size_t kDashPos1 = 8;
    constexpr size_t kDashPos2 = 13;
    constexpr size_t kDashPos3 = 18;
    constexpr size_t kDashPos4 = 23;
    constexpr auto kUuidDashPositions
      = std::to_array<size_t>({ kDashPos1, kDashPos2, kDashPos3, kDashPos4 });
    if (text.size() != kUuidStringLength) {
      return false;
    }

    for (const auto pos : kUuidDashPositions) {
      if (text.at(pos) != '-') {
        return false;
      }
    }

    size_t src = 0;
    size_t dst = 0;
    while (src < text.size() && dst < out.size()) {
      if (text.at(src) == '-') {
        ++src;
        continue;
      }
      if (src + 1 >= text.size()) {
        return false;
      }
      const char hi = text.at(src);
      const char lo = text.at(src + 1);
      if (!IsHexChar(hi) || !IsHexChar(lo)) {
        return false;
      }
      out.at(dst++) = static_cast<uint8_t>(
        (HexValue(hi) << kHexNibbleShift) | HexValue(lo));
      src += 2;
    }
    return dst == out.size();
  }

  auto BytesToUuidString(const UuidBytes& bytes) -> std::string
  {
    constexpr size_t kDashAfterByte1 = 4;
    constexpr size_t kDashAfterByte2 = 6;
    constexpr size_t kDashAfterByte3 = 8;
    constexpr size_t kDashAfterByte4 = 10;
    constexpr auto kUuidDashByteIndices = std::to_array<size_t>(
      { kDashAfterByte1, kDashAfterByte2, kDashAfterByte3, kDashAfterByte4 });
    std::string out;
    out.reserve(kUuidStringLength);
    for (size_t i = 0; i < bytes.size(); ++i) {
      if (std::find(kUuidDashByteIndices.begin(), kUuidDashByteIndices.end(), i)
        != kUuidDashByteIndices.end()) {
        out.push_back('-');
      }
      const auto b = bytes.at(i);
      out.push_back(kHexDigits.at(
        static_cast<size_t>((b >> kHexNibbleShift) & kHexNibbleMask)));
      out.push_back(kHexDigits.at(static_cast<size_t>(b & kHexNibbleMask)));
    }
    return out;
  }

  auto LuaUuidNew(lua_State* state) -> int
  {
    const auto bytes = data::GenerateAssetGuid();
    const auto text = BytesToUuidString(bytes);
    lua_pushlstring(state, text.c_str(), text.size());
    return 1;
  }

  auto LuaUuidIsValid(lua_State* state) -> int
  {
    size_t text_size = 0;
    const auto* text_ptr = lua_tolstring(state, kLuaArg1, &text_size);
    if (text_ptr == nullptr) {
      lua_pushboolean(state, 0);
      return 1;
    }
    UuidBytes bytes {};
    lua_pushboolean(
      state, ParseUuidString({ text_ptr, text_size }, bytes) ? 1 : 0);
    return 1;
  }

  auto LuaUuidToString(lua_State* state) -> int
  {
    size_t text_size = 0;
    const auto* text_ptr = lua_tolstring(state, kLuaArg1, &text_size);
    if (text_ptr == nullptr) {
      return 0;
    }

    UuidBytes bytes {};
    if (!ParseUuidString({ text_ptr, text_size }, bytes)) {
      luaL_error(state, "invalid uuid format");
      return 0;
    }

    const auto normalized = BytesToUuidString(bytes);
    lua_pushlstring(state, normalized.c_str(), normalized.size());
    return 1;
  }

  auto LuaUuidFromString(lua_State* state) -> int
  {
    return LuaUuidToString(state);
  }

  auto LuaHash64(lua_State* state) -> int
  {
    size_t text_size = 0;
    const auto* text_ptr = lua_tolstring(state, kLuaArg1, &text_size);
    if (text_ptr == nullptr) {
      return 0;
    }
    const auto h = ComputeFNV1a64(text_ptr, text_size);
    lua_pushinteger(state, static_cast<lua_Integer>(h));
    return 1;
  }

  auto LuaHashCombine64(lua_State* state) -> int
  {
    if ((lua_isnumber(state, kLuaArg1) == 0)
      || (lua_isnumber(state, kLuaArg2) == 0)) {
      return 0;
    }
    size_t seed = static_cast<size_t>(
      static_cast<std::uint64_t>(lua_tointeger(state, kLuaArg1)));
    const auto value
      = static_cast<std::uint64_t>(lua_tointeger(state, kLuaArg2));
    HashCombine(seed, value);
    lua_pushinteger(state, static_cast<lua_Integer>(seed));
    return 1;
  }
} // namespace

auto RegisterUuidBindings(lua_State* state, const int oxygen_table_index)
  -> void
{
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
  const int module_index
    = PushOxygenSubtable(state, oxygen_table_index, "hash");

  lua_pushcfunction(state, LuaHash64, "hash.hash64");
  lua_setfield(state, module_index, "hash64");
  lua_pushcfunction(state, LuaHashCombine64, "hash.combine64");
  lua_setfield(state, module_index, "combine64");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
