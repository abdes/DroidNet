#pragma once
#pragma unmanaged

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module {

class ViewManager;

class DestroyViewCommand final : public EditorCommand {
public:
  DestroyViewCommand(ViewManager* mgr, ViewId id) : manager_(mgr), view_id_(id) {}
  void Execute(CommandContext& ctx) override;

private:
  ViewManager* manager_{ nullptr };
  ViewId view_id_{};
};

} // namespace oxygen::interop::module
