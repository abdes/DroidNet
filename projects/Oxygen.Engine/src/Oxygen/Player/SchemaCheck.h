//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Macros.h>

namespace nlohmann::json_schema {
class json_validator;
} // namespace nlohmann::json_schema

namespace oxygen::player {

//! Validates JSON values against one embedded schema.
class SchemaCheck final {
public:
  //! \throws std::exception when `schema` is not a valid schema.
  explicit SchemaCheck(std::string_view schema);
  ~SchemaCheck();

  OXYGEN_MAKE_NON_COPYABLE(SchemaCheck)
  OXYGEN_MAKE_NON_MOVABLE(SchemaCheck)

  //! One `<json pointer>: <message>` per violation; empty when valid.
  [[nodiscard]] auto Validate(const nlohmann::json& value) const
    -> std::vector<std::string>;

private:
  std::unique_ptr<nlohmann::json_schema::json_validator> validator_;
};

//! The launch file schema.
[[nodiscard]] auto LaunchSchema() -> const SchemaCheck&;
//! The request schema.
[[nodiscard]] auto RequestSchema() -> const SchemaCheck&;
//! The event schema.
[[nodiscard]] auto EventSchema() -> const SchemaCheck&;

} // namespace oxygen::player
