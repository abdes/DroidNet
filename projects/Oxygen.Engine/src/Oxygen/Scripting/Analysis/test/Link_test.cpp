//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>
#include <string_view>

#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  constexpr std::string_view kSource = "return {}";
  const auto analysis = oxygen::scripting::ScriptAnalyzer {}.Analyze({
    .source_name = "link_test.lua",
    .source = std::as_bytes(std::span(kSource.data(), kSource.size())),
    .level = oxygen::scripting::AnalysisLevel::kDeclarations,
  });
  return analysis.HasErrors() ? 1 : 0;
}
