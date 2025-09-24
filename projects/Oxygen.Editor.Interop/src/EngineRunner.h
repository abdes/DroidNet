//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace Oxygen::Editor::EngineInterface {

public
ref class LoggingConfig sealed {
public:
  LoggingConfig() {
    // default verbosity (OFF)
    Verbosity = -9;
    IsColored = false;
    ModuleOverrides = gcnew System::String("");
  }

  property int Verbosity;
  property bool IsColored;
  property System::String ^ ModuleOverrides;
};

public
ref class EngineConfig sealed{
  public : EngineConfig(){}

};

public
ref class EngineRunner sealed {
public:
  static auto ConfigureLogging(LoggingConfig ^ config) -> bool;

  /// <summary>
  ///  Creates the engine with the specified configuration.
  /// </summary>
  static auto CreateEngine(EngineConfig ^ config) -> bool;
};

} // namespace Oxygen::Editor::EngineInterface
