//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/api_export.h>

namespace oxygen::commands {

enum class ParamType : uint8_t {
  kBool,
  kInt,
  kDouble,
  kString,
  //! A string restricted to `CommandParam::allowed_values`.
  kEnum,
  //! A file system path, kept as text.
  kPath,
  //! Any of bool, integer, number or string; the handler interprets it.
  kScalar,
};

using ParamValue = std::variant<bool, int64_t, double, std::string>;

//! One parameter of a command.
/*!
 The list of parameters is the command's whole argument contract: console
 text and JSON are both bound through it, it generates the JSON Schema that
 remote clients see, and it drives help and completion.
*/
struct CommandParam {
  std::string name;
  ParamType type { ParamType::kString };
  std::string help;
  bool required { false };
  std::optional<ParamValue> default_value {}; // NOLINT(*-member-init)
  std::optional<double> min_value {}; // NOLINT(*-member-init)
  std::optional<double> max_value {}; // NOLINT(*-member-init)
  std::vector<std::string> allowed_values {}; // NOLINT(*-member-init)
  //! Only for the last string or path parameter: takes the rest of the line.
  bool rest_of_line { false };
};

//! Arguments of one invocation, bound and validated against its parameters.
class CommandArgs final {
public:
  OXGN_CMDS_NDAPI auto Has(std::string_view name) const noexcept -> bool;
  OXGN_CMDS_NDAPI auto Find(std::string_view name) const noexcept
    -> observer_ptr<const ParamValue>;

  //! Typed access to a parameter that is present; a missing parameter or a
  //! different type is a programming error.
  OXGN_CMDS_NDAPI auto GetBool(std::string_view name) const -> bool;
  OXGN_CMDS_NDAPI auto GetInt(std::string_view name) const -> int64_t;
  OXGN_CMDS_NDAPI auto GetDouble(std::string_view name) const -> double;
  OXGN_CMDS_NDAPI auto GetString(std::string_view name) const
    -> const std::string&;

  OXGN_CMDS_API auto Set(std::string name, ParamValue value) -> void;
  OXGN_CMDS_NDAPI auto ToJson() const -> nlohmann::json;
  OXGN_CMDS_NDAPI auto Values() const noexcept
    -> std::span<const std::pair<std::string, ParamValue>>
  {
    return values_;
  }

private:
  std::vector<std::pair<std::string, ParamValue>> values_;
};

//! Why arguments could not be bound; one message per problem.
struct ArgumentErrors {
  std::vector<std::string> messages;
};

//! Bind console tokens (the words after the command name).
/*!
 Tokens are positional in declaration order; `name=value` names a parameter
 explicitly. A `rest_of_line` parameter takes the remaining positional tokens
 joined by single spaces.
*/
OXGN_CMDS_NDAPI auto BindText(std::span<const CommandParam> params,
  std::span<const std::string> tokens) -> Result<CommandArgs, ArgumentErrors>;

//! Bind a JSON object (or null for no arguments).
OXGN_CMDS_NDAPI auto BindJson(std::span<const CommandParam> params,
  const nlohmann::json& arguments) -> Result<CommandArgs, ArgumentErrors>;

//! JSON Schema of a JSON argument object for these parameters.
OXGN_CMDS_NDAPI auto ParamsSchema(std::span<const CommandParam> params)
  -> nlohmann::json;

OXGN_CMDS_NDAPI auto to_string(ParamType value) -> const char*;

} // namespace oxygen::commands
