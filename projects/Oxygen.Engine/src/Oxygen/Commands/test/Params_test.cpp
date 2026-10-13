//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Commands/Params.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::commands::BindJson;
using oxygen::commands::BindText;
using oxygen::commands::CommandParam;
using oxygen::commands::ParamsSchema;
using oxygen::commands::ParamType;
using oxygen::commands::ParamValue;
using testing::ElementsAre;
using testing::HasSubstr;

//! `frames` (int, 1..1000, default 1), `mode` (enum), `note` (rest of line).
auto StepParams() -> std::vector<CommandParam>
{
  return {
    CommandParam {
      .name = "frames",
      .type = ParamType::kInt,
      .help = "Frames to simulate",
      .default_value = ParamValue { int64_t { 1 } },
      .min_value = 1.0,
      .max_value = 1000.0,
    },
    CommandParam {
      .name = "mode",
      .type = ParamType::kEnum,
      .default_value = ParamValue { std::string { "fixed" } },
      .allowed_values = { "fixed", "real" },
    },
    CommandParam {
      .name = "note",
      .type = ParamType::kString,
      .default_value = ParamValue { std::string {} },
      .rest_of_line = true,
    },
  };
}

//! Text and JSON with the same meaning bind to the same arguments.
NOLINT_TEST(CommandParams, TextAndJsonBindTheSameArguments)
{
  const auto params = StepParams();
  const auto tokens = std::vector<std::string> { "3", "real", "a", "b" };

  const auto from_text = BindText(params, tokens);
  const auto from_json = BindJson(
    params, json { { "frames", 3 }, { "mode", "real" }, { "note", "a b" } });

  ASSERT_TRUE(from_text);
  ASSERT_TRUE(from_json);
  EXPECT_EQ(from_text.value().ToJson(), from_json.value().ToJson());
  EXPECT_EQ(from_text.value().GetInt("frames"), 3);
  EXPECT_EQ(from_text.value().GetString("note"), "a b");
}

//! Missing parameters take their defaults; named text arguments work.
NOLINT_TEST(CommandParams, DefaultsAndNamedTextArguments)
{
  const auto params = StepParams();
  const auto tokens = std::vector<std::string> { "mode=real" };

  const auto args = BindText(params, tokens);

  ASSERT_TRUE(args);
  EXPECT_EQ(args.value().GetInt("frames"), 1);
  EXPECT_EQ(args.value().GetString("mode"), "real");
}

//! Out-of-range and disallowed values fail with the same messages from text
//! and from JSON.
NOLINT_TEST(CommandParams, RangeAndEnumErrorsMatchAcrossSyntaxes)
{
  const auto params = StepParams();
  const auto tokens = std::vector<std::string> { "0", "slow" };

  const auto from_text = BindText(params, tokens);
  const auto from_json
    = BindJson(params, json { { "frames", 0 }, { "mode", "slow" } });

  ASSERT_FALSE(from_text);
  ASSERT_FALSE(from_json);
  EXPECT_EQ(from_text.error().messages, from_json.error().messages);
  EXPECT_THAT(from_text.error().messages,
    ElementsAre(HasSubstr("below the minimum"), HasSubstr("is not one of")));
}

//! Wrong types, unknown names and missing required parameters are errors.
NOLINT_TEST(CommandParams, RejectsBadArguments)
{
  const auto params = std::vector<CommandParam> {
    CommandParam { .name = "on", .type = ParamType::kBool, .required = true },
  };
  const auto tokens = std::vector<std::string> { "maybe" };

  const auto bad_text = BindText(params, tokens);
  const auto bad_json = BindJson(params, json { { "on", 1 }, { "x", true } });
  const auto missing = BindJson(params, json::object());

  ASSERT_FALSE(bad_text);
  EXPECT_THAT(
    bad_text.error().messages, ElementsAre(HasSubstr("expected a boolean")));
  ASSERT_FALSE(bad_json);
  EXPECT_EQ(bad_json.error().messages.size(), 2U);
  ASSERT_FALSE(missing);
  EXPECT_THAT(missing.error().messages,
    ElementsAre(HasSubstr("missing required argument")));
}

//! The generated schema describes types, defaults, ranges and enums.
NOLINT_TEST(CommandParams, SchemaDescribesParameters)
{
  const auto schema = ParamsSchema(StepParams());

  EXPECT_EQ(schema["type"], "object");
  EXPECT_EQ(schema["additionalProperties"], false);
  EXPECT_EQ(schema["properties"]["frames"]["type"], "integer");
  EXPECT_EQ(schema["properties"]["frames"]["default"], 1);
  EXPECT_EQ(schema["properties"]["frames"]["minimum"], 1.0);
  EXPECT_EQ(
    schema["properties"]["mode"]["enum"], json::array({ "fixed", "real" }));
  EXPECT_FALSE(schema.contains("required"));
}

} // namespace
