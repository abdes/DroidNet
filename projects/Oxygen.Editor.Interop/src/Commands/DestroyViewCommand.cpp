#include "pch.h"
#include "Commands/DestroyViewCommand.h"
#include "EditorModule/ViewManager.h"

namespace oxygen::interop::module {

void DestroyViewCommand::Execute(CommandContext& /*context*/) {
  DLOG_SCOPE_F(4, "DestroyViewCommand::Execute");

  if (manager_ == nullptr) {
    DLOG_F(WARNING, "DestroyViewCommand: ViewManager null");
    return;
  }

  manager_->DestroyView(view_id_);
  LOG_F(INFO, "DestroyViewCommand: view {} destroyed", view_id_.get());
}

} // namespace oxygen::interop::module
