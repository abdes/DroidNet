//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::JsonSchemaValidationDiagnosticConfig;
  using internal::MakeJsonSchemaValidator;
  using internal::ValidateJsonSchema;
  using internal::ValidateJsonSchemaWithDiagnostics;
  using nlohmann::json;

  struct Emitted final {
    std::string code;
    std::string message;
    std::string json_pointer;
  };

  constexpr auto kConfig = JsonSchemaValidationDiagnosticConfig {
    .validation_failed_code = "test.schema_validation_failed",
    .validation_failed_prefix = "invalid: ",
    .validator_failure_code = "test.schema_validator_failure",
    .validator_failure_prefix = "validator: ",
  };

  auto Validate(const std::string_view schema, const json& instance)
    -> std::vector<Emitted>
  {
    auto validator = MakeJsonSchemaValidator(json::parse(schema));
    auto emitted = std::vector<Emitted> {};
    [[maybe_unused]] const auto accepted
      = ValidateJsonSchemaWithDiagnostics(validator, instance, kConfig,
        [&emitted](const std::string_view code, const std::string& message,
          const std::string& json_pointer) -> void {
          emitted.push_back({
            .code = std::string(code),
            .message = message,
            .json_pointer = json_pointer,
          });
        });
    return emitted;
  }

  //! Every issue is emitted, however many there are, each at its value.
  NOLINT_TEST(JsonSchemaValidationTest, EmitsEveryIssueAtItsValue)
  {
    constexpr std::size_t kInvalidEntries = 13;
    constexpr auto kSchema = std::string_view { R"({
      "type": "object",
      "properties": {
        "actions": { "type": "array", "items": { "type": "integer" } }
      }
    })" };
    auto instance = json { { "actions", json::array() } };
    for (std::size_t i = 0; i < kInvalidEntries; ++i) {
      instance.at("actions").push_back("not an integer");
    }

    const auto emitted = Validate(kSchema, instance);

    ASSERT_EQ(emitted.size(), kInvalidEntries);
    for (std::size_t i = 0; i < kInvalidEntries; ++i) {
      EXPECT_EQ(emitted.at(i).code, "test.schema_validation_failed");
      EXPECT_EQ(emitted.at(i).json_pointer, "/actions/" + std::to_string(i));
    }
  }

  //! Keys holding `.`, `/`, `~` or digits keep a pointer that names them
  //! alone.
  NOLINT_TEST(JsonSchemaValidationTest, PointersEscapeKeys)
  {
    constexpr auto kSchema = std::string_view { R"({
      "type": "object",
      "properties": {
        "a.b": { "type": "integer" },
        "a/b": { "type": "integer" },
        "t~x": { "type": "integer" },
        "0": { "type": "integer" }
      }
    })" };
    const auto instance = json {
      { "a.b", "x" },
      { "a/b", "x" },
      { "t~x", "x" },
      { "0", "x" },
    };

    const auto emitted = Validate(kSchema, instance);

    auto pointers = std::vector<std::string> {};
    for (const auto& issue : emitted) {
      pointers.push_back(issue.json_pointer);
    }
    std::ranges::sort(pointers);
    EXPECT_EQ(
      pointers, (std::vector<std::string> { "/0", "/a.b", "/a~1b", "/t~0x" }));
    for (const auto& pointer : pointers) {
      EXPECT_TRUE(instance.contains(json::json_pointer { pointer }));
    }
  }

  //! A refused key is located at its object and named by its message.
  NOLINT_TEST(JsonSchemaValidationTest, RefusedKeyIsNamedInTheMessage)
  {
    const auto emitted = Validate(R"({
      "type": "object",
      "propertyNames": { "enum": ["known"] }
    })",
      json { { "unknown_key", 1 } });

    ASSERT_EQ(emitted.size(), 1U);
    EXPECT_TRUE(emitted.front().json_pointer.empty());
    EXPECT_NE(emitted.front().message.find("unknown_key"), std::string::npos);
  }

  //! A value the root itself breaks is located by the empty pointer.
  NOLINT_TEST(JsonSchemaValidationTest, RootIssueHasEmptyPointer)
  {
    const auto emitted = Validate(R"({ "type": "object" })", json::array());

    ASSERT_EQ(emitted.size(), 1U);
    EXPECT_TRUE(emitted.front().json_pointer.empty());
  }

  //! The throwing form names every issue by its pointer.
  NOLINT_TEST(JsonSchemaValidationTest, ThrowingFormNamesEveryIssue)
  {
    constexpr std::size_t kInvalidEntries = 5;
    auto validator = MakeJsonSchemaValidator(json::parse(R"({
      "type": "array",
      "items": { "type": "integer" }
    })"));
    auto instance = json::array();
    for (std::size_t i = 0; i < kInvalidEntries; ++i) {
      instance.push_back("not an integer");
    }

    try {
      ValidateJsonSchema(validator, instance, "Test document");
      FAIL() << "The document was accepted";
    } catch (const std::invalid_argument& error) {
      const auto message = std::string_view { error.what() };
      for (std::size_t i = 0; i < kInvalidEntries; ++i) {
        EXPECT_NE(
          message.find("/" + std::to_string(i) + ": "), std::string_view::npos)
          << message;
      }
    }
  }

} // namespace
} // namespace oxygen::content::import::test
