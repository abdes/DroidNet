//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen::input {

class Action;
class InputMappingContext;

//! Input actions and mapping contexts created in code.
/*!
 An action or context created in code, not hydrated from an asset, is
 identified by a code key (`data::AssetKey::FromCodePath`). `owner` is the path
 of whoever creates it, such as `Editor/Viewport`; `name` is both its label and
 the leaf of its key, which carries the extension of the asset type it stands
 in for. A caller spells each identity once: its owner constant and its name.
*/

//! An action `owner` creates in code, labelled `name`.
OXGN_NPUT_NDAPI auto MakeCodeAction(std::string_view owner, std::string name,
  ActionValueType value_type) -> std::shared_ptr<Action>;

//! A mapping context `owner` creates in code, labelled `name`.
OXGN_NPUT_NDAPI auto MakeCodeContext(std::string_view owner, std::string name)
  -> std::shared_ptr<InputMappingContext>;

} // namespace oxygen::input
