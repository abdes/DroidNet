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
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

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

} // namespace oxygen::scripting::test
