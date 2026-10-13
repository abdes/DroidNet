//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class UuidHashBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(UuidHashBindingsTest, ExecuteScriptUuidAndHashBindingsWork)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local id = oxygen.uuid.new()
if not oxygen.uuid.is_valid(id) then
  error("uuid.new produced invalid uuid")
end

local normalized = oxygen.uuid.to_string(id)
if not oxygen.uuid.is_valid(normalized) then
  error("uuid.to_string produced invalid uuid")
end

local parsed = oxygen.uuid.from_string(normalized)
if tostring(parsed) ~= normalized then
  error("uuid.from_string mismatch")
end

local h1 = oxygen.hash.hash64("oxygen")
local h2 = oxygen.hash.hash64("engine")
local hc = oxygen.hash.combine64(h1, h2)
if hc == h1 or hc == h2 then
  error("hash.combine64 did not combine values")
end
)lua" },
    .chunk_name = ScriptChunkName { "uuid_hash_bindings" },
  });

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(UuidHashBindingsTest, UuidHashBindingsRejectInvalidArgumentShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ok = pcall(function()
  oxygen.uuid.to_string("not_uuid_userdata")
end)
if ok then
  error("uuid.to_string must reject non-uuid userdata")
end

ok = pcall(function()
  oxygen.uuid.from_string(123)
end)
if ok then
  error("uuid.from_string must reject non-string input")
end

ok = pcall(function()
  oxygen.hash.hash64(42)
end)
if ok then
  error("hash.hash64 must reject non-string input")
end

ok = pcall(function()
  oxygen.hash.combine64(1.25, 2)
end)
if ok then
  error("hash.combine64 must reject non-integer numbers")
end
)lua" },
    .chunk_name = ScriptChunkName { "uuid_hash_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(UuidHashBindingsTest, UuidFromStringInvalidTextReturnsNil)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local parsed = oxygen.uuid.from_string("invalid-uuid-text")
if parsed ~= nil then
  error("uuid.from_string must return nil for invalid uuid text")
end
)lua" },
    .chunk_name = ScriptChunkName { "uuid_from_string_invalid_nil" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(UuidHashBindingsTest, ExecuteScriptUuidMetamethodsWork)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local id1 = oxygen.uuid.new()
local id2 = oxygen.uuid.new()

-- __eq metamethod
if id1 == id2 then
  error("uuid __eq failed: distinct UUIDs should not be equal")
end

local str1 = tostring(id1)
local id3 = oxygen.uuid.from_string(str1)

if id1 ~= id3 then
  error("uuid __eq failed: identical UUIDs should be equal")
end

-- __tostring metamethod implicitly used above
if type(str1) ~= "string" or string.len(str1) ~= 36 then
  error("uuid __tostring failed")
end
)lua" },
    .chunk_name = ScriptChunkName { "uuid_metamethods" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(UuidHashBindingsTest, ExecuteScriptHashMetamethodsAndCoercion)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local h1 = oxygen.hash.hash64("oxygen")
local h2 = oxygen.hash.hash64("oxygen")
local h3 = oxygen.hash.hash64("engine")

-- __eq metamethod
if h1 ~= h2 then
  error("hash __eq failed: identical hashes should be equal")
end
if h1 == h3 then
  error("hash __eq failed: distinct hashes should not be equal")
end

-- __tostring metamethod
local str_h1 = tostring(h1)
if type(str_h1) ~= "string" or string.len(str_h1) < 1 then
  error("hash __tostring failed")
end

-- hash.combine64 uses CheckHash which supports (HashUserData, String, Integer)
-- Let's test combinations
local c1 = oxygen.hash.combine64(h1, h3)
local c2 = oxygen.hash.combine64("oxygen", "engine")
local c3 = oxygen.hash.combine64(h1, "engine")

if c1 ~= c2 or c2 ~= c3 then
  error("hash.combine64 coercion failed")
end

-- Test Number Coercion (exact integers)
local int_val = 123456
local c4 = oxygen.hash.combine64(int_val, "engine")
local c5 = oxygen.hash.combine64(int_val, h3)
if c4 ~= c5 then
  error("hash.combine64 number coercion failed")
end
)lua" },
    .chunk_name = ScriptChunkName { "hash_metamethods" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(UuidHashBindingsTest, ExecuteScriptIsValidValidatesThoroughly)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
-- valid uuid string
if not oxygen.uuid.is_valid("018f4a7c-7b2d-7a3c-8f01-3b7e2f9c4d10") then
  error("is_valid failed for correct uuid string")
end

-- invalid length
if oxygen.uuid.is_valid("12345678-1234-1234-1234-123456789ab") then
  error("is_valid should reject wrong length")
end

-- invalid dashes
if oxygen.uuid.is_valid("12345678x1234-1234-1234-123456789abc") then
  error("is_valid should reject invalid dash positions")
end

-- invalid hex
if oxygen.uuid.is_valid("12345678-1234-1234-1234-123456789abg") then
  error("is_valid should reject invalid hex characters")
end

-- invalid version (must be v7)
if oxygen.uuid.is_valid("12345678-1234-1234-8abc-123456789abc") then
  error("is_valid should reject non-v7 uuid version")
end

-- invalid variant (must be RFC4122 10xx)
if oxygen.uuid.is_valid("018f4a7c-7b2d-7a3c-4f01-3b7e2f9c4d10") then
  error("is_valid should reject invalid uuid variant")
end

-- invalid type
if oxygen.uuid.is_valid(12345) then
  error("is_valid should reject numbers")
end
)lua" },
    .chunk_name = ScriptChunkName { "uuid_is_valid_thorough" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
