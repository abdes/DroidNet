//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module {

class ViewManager;

class HideViewCommand : public EditorCommand {
public:
  explicit HideViewCommand(ViewManager* manager, ViewId view_id)
      : manager_(manager), view_id_(view_id) {}

  void Execute(CommandContext& /*context*/) override;

private:
  ViewManager* manager_;
  ViewId view_id_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
