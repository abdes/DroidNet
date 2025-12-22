//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>

#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/PhaseRegistry.h>

namespace oxygen::interop::module {

class EditorModule;

class CreateSceneCommand final : public EditorCommand {
public:
  using OnComplete = std::function<void(bool)>;

  CreateSceneCommand(EditorModule *module, std::string name,
                     OnComplete cb = nullptr)
      : EditorCommand(oxygen::core::PhaseId::kFrameStart), module_(module),
        name_(std::move(name)), cb_(std::move(cb)) {}

  void Execute(CommandContext &context) override;

private:
  EditorModule *module_;
  std::string name_;
  OnComplete cb_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
