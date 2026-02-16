//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace oxygen::scripting {

struct ScriptLoadResult {
  bool ok { false };
  std::string source_text;
  std::string chunk_name;
  std::string error_message;
};

class IScriptLoader {
public:
  IScriptLoader() = default;
  virtual ~IScriptLoader() = default;

  IScriptLoader(const IScriptLoader&) = delete;
  IScriptLoader(IScriptLoader&&) = delete;
  auto operator=(const IScriptLoader&) -> IScriptLoader& = delete;
  auto operator=(IScriptLoader&&) -> IScriptLoader& = delete;

  [[nodiscard]] virtual auto LoadScript(std::string_view script_id) const
    -> ScriptLoadResult
    = 0;
};

} // namespace oxygen::scripting
