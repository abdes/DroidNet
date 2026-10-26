//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#pragma managed(push, off)

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <EditorModule/EditorCommand.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/VirtualPathResolver.h>

namespace oxygen::interop::module {

//! Reads the hierarchy of a scene of the mounted content, with every scene it
//! instances, from the scene descriptors alone.
/*!
 The callback runs on the engine thread with no value when the virtual path
 names no mounted scene. A command dropped without running never calls back.
*/
class ReadSceneHierarchyCommand final : public EditorCommand {
public:
  using Callback
    = std::function<void(std::optional<content::IAssetLoader::SceneHierarchy>)>;

  ReadSceneHierarchyCommand(std::string virtual_path, Callback complete)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , virtual_path_(std::move(virtual_path))
    , complete_(std::move(complete))
  {
  }

  //! Only reads mounted content; changes nothing a pane shows.
  [[nodiscard]] auto GetInvalidation() const noexcept
    -> CommandInvalidation override
  {
    return CommandInvalidation::None();
  }

  void Execute(CommandContext& context) override
  {
    if (!context.AssetLoader || !context.PathResolver) {
      complete_(std::nullopt);
      return;
    }
    const auto key = context.PathResolver->ResolveAssetKey(virtual_path_);
    if (!key.has_value()) {
      complete_(std::nullopt);
      return;
    }
    complete_(context.AssetLoader->ReadMountedSceneHierarchy(*key));
  }

private:
  std::string virtual_path_;
  Callback complete_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
