//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>
#include <string_view>

namespace oxygen::input {
  class Action;
} // namespace oxygen::input

namespace oxygen::interop::module {

  //! The owner of the inputs the editor viewport creates in code (see
  //! `input::MakeCodeAction`).
  inline constexpr std::string_view kEditorViewportInputOwner
    = "Editor/Viewport";

  //! The inputs several navigation features read.
  /*!
   EditorViewportNavigation creates and maps them once, before any feature
   registers, so no feature depends on another having registered first.
  */
  struct EditorViewportInputActions {
    std::shared_ptr<oxygen::input::Action> alt;
    std::shared_ptr<oxygen::input::Action> left_button;
    std::shared_ptr<oxygen::input::Action> middle_button;
    std::shared_ptr<oxygen::input::Action> right_button;
    std::shared_ptr<oxygen::input::Action> mouse_delta;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
