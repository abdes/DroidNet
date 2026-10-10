//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

//! Parses a JSON file. Throws `std::runtime_error` naming the path on failure.
[[nodiscard]] auto LoadJson(const std::filesystem::path& path)
  -> nlohmann::json;

//! Loads a schema by its path relative to `Cooker/` (see `SchemaPath`).
[[nodiscard]] auto LoadSchema(std::string_view relative_to_cooker)
  -> nlohmann::json;

//! Validates `document` against `schema`. An empty result means valid; each
//! entry reads `"<json-pointer>: <message>"`. A schema that cannot be
//! compiled is reported as a single entry.
[[nodiscard]] auto ValidateJson(const nlohmann::json& schema,
  const nlohmann::json& document) -> std::vector<std::string>;

//! One row of a table-driven schema test.
struct SchemaCase {
  //! CamelCase suffix for the generated test name.
  std::string_view name;
  //! Document text.
  std::string_view json;
  bool valid = true;
  //! Required when `valid` is false: a substring of the expected error.
  std::string_view error_substr;
};

//! Test-name generator for `INSTANTIATE_TEST_SUITE_P` over `SchemaCase`.
[[nodiscard]] auto SchemaCaseName(
  const ::testing::TestParamInfo<SchemaCase>& info) -> std::string;

//! Checks one `SchemaCase` against `schema` with `EXPECT_*`.
auto ExpectSchemaCase(const nlohmann::json& schema, const SchemaCase& c)
  -> void;

} // namespace oxygen::cooker::test
