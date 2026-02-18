//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class FailedScriptExecutable final : public ScriptExecutable {
public:
  explicit FailedScriptExecutable(std::string diagnostic_message)
    : diagnostic_message_(std::move(diagnostic_message))
  {
  }

  OXGN_SCRP_API auto Run() const noexcept -> void override { }

  [[nodiscard]] auto Diagnostic() const noexcept -> std::string_view
  {
    return diagnostic_message_;
  }

private:
  std::string diagnostic_message_;
};

} // namespace oxygen::scripting
