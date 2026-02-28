//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Engine/Scripting/IScriptCompiler.h>

namespace {

template <typename T> struct CompileModeFromCompileSignature;

template <typename ReturnT, typename ClassT, typename SourceT, typename ModeT>
struct CompileModeFromCompileSignature<ReturnT (ClassT::*)(SourceT, ModeT)
    const> {
  using Type = ModeT;
};

using CoreScriptCompileMode = oxygen::core::meta::scripting::ScriptCompileMode;
using RequestScriptCompileMode = decltype(std::declval<
  oxygen::scripting::IScriptCompilationService::Request>()
    .compile_mode);
using InterfaceScriptCompileMode = typename CompileModeFromCompileSignature<
  decltype(&oxygen::scripting::IScriptCompiler::Compile)>::Type;

static_assert(std::is_same_v<CoreScriptCompileMode, RequestScriptCompileMode>);
static_assert(
  std::is_same_v<CoreScriptCompileMode, InterfaceScriptCompileMode>);

NOLINT_TEST(ScriptCompileModeContractTest, CoreMetaMappingStaysStable)
{
  using oxygen::core::meta::scripting::to_string;
  EXPECT_EQ(to_string(CoreScriptCompileMode::kDebug), "debug");
  EXPECT_EQ(to_string(CoreScriptCompileMode::kOptimized), "optimized");
}

} // namespace
