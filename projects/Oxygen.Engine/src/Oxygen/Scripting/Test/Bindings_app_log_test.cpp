//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class AppLogBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(AppLogBindingsTest, ExecuteScriptAppBindingWorksWithFakeEngine)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local _ = oxygen.app.name()
)lua" },
    .chunk_name = ScriptChunkName { "app_fake_engine" },
  });
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(AppLogBindingsTest, ExecuteScriptLogBindingWorksWithFakeEngine)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.log.info("hello")
)lua" },
    .chunk_name = ScriptChunkName { "log_no_engine" },
  });
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
}

} // namespace oxygen::scripting::test
