//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/EditorInterface/Api.h>

#include "EngineRunner.h"

namespace Oxygen::Editor::EngineInterface {

auto EngineRunner::ConfigureLogging(LoggingConfig ^ config) -> bool {
  namespace op = oxygen::engine::interop;

  // Prepare native struct
  op::LoggingConfig native_config;
  native_config.verbosity = config->Verbosity;
  native_config.is_colored = config->IsColored;
  native_config.vmodules = nullptr; // default

  // Marshal managed string to std::string if provided.
  std::string vmodules;
  if (config->ModuleOverrides != nullptr) {
    vmodules = msclr::interop::marshal_as<std::string>(config->ModuleOverrides);
    // Set native pointer only if the marshaled string is non-empty.
    if (!vmodules.empty()) {
      native_config.vmodules = vmodules.c_str();
    }
  }

  const bool result = op::ConfigureLogging(native_config);
  return result;
}

auto EngineRunner::CreateEngine(EngineConfig ^ config) -> bool { return false; }

} // namespace Oxygen::Editor::EngineInterface
