//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>

namespace oxygen::cooker::testing {

//! A cooked script descriptor, as the script cook writes it.
/*!
 Its payloads are rows of the root's script table
 (`LooseCookedRoot::AddScript`); the descriptor names them through its
 references, as a cook does. A script with neither payload is a script whose
 source is only external.

 ```cpp
 auto game = LooseCookedRoot { "Game" };
 const auto bytecode = game.AddScript(SeedBytes(1));
 auto script = ScriptBuilder { "Spin" };
 script.Bytecode(bytecode).Parameters({ { "speed", 2.0F } });
 game.Script("Scripts/Spin.oscript", script);
 ```
*/
class ScriptBuilder final {
public:
  explicit ScriptBuilder(std::string_view name = "Script");

  //! The bytecode payload: row `row` of the script table.
  auto Bytecode(ResourceIndexT row) -> ScriptBuilder&;

  //! The source payload: row `row` of the script table.
  auto Source(ResourceIndexT row) -> ScriptBuilder&;

  //! The mount path of the script's external source, `/<Mount>/<path>`.
  auto ExternalSource(std::string mount_path) -> ScriptBuilder&;

  auto Flags(data::pak::scripting::ScriptAssetFlags flags) -> ScriptBuilder&;

  //! The declared parameters and their defaults.
  auto Parameters(data::ScriptParameters parameters) -> ScriptBuilder&;

  [[nodiscard]] auto Name() const noexcept -> const std::string&
  {
    return name_;
  }

  //! The descriptor.
  /*!
   @throw std::logic_error When the product loader refuses it.
  */
  [[nodiscard]] auto Bytes() const -> std::vector<std::byte>;

  //! The references a cook records of the descriptor: its payload rows.
  [[nodiscard]] auto References() const -> data::AssetReferences;

private:
  std::string name_;
  std::optional<ResourceIndexT> bytecode_;
  std::optional<ResourceIndexT> source_;
  std::string external_source_;
  data::pak::scripting::ScriptAssetFlags flags_
    = data::pak::scripting::ScriptAssetFlags::kNone;
  data::ScriptParameters parameters_;
};

} // namespace oxygen::cooker::testing
