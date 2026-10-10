//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json-schema.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>

namespace oxygen::cooker::test {

namespace {

  class CollectingErrorHandler final
    : public nlohmann::json_schema::error_handler {
  public:
    void error(const nlohmann::json::json_pointer& ptr,
      const nlohmann::json& instance, const std::string& message) override
    {
      auto out = std::ostringstream {};
      const auto path = ptr.to_string();
      out << (path.empty() ? "<root>" : path) << ": " << message;
      if (!instance.is_discarded()) {
        out << " (value=" << instance.dump() << ")";
      }
      errors_.push_back(out.str());
    }

    [[nodiscard]] auto TakeErrors() -> std::vector<std::string>
    {
      return std::move(errors_);
    }

  private:
    std::vector<std::string> errors_;
  };

} // namespace

auto LoadJson(const std::filesystem::path& path) -> nlohmann::json
{
  auto in = std::ifstream(base::ToNativePath(path));
  if (!in) {
    throw std::runtime_error("Cannot open JSON file: " + path.string());
  }
  try {
    return nlohmann::json::parse(in);
  } catch (const std::exception& ex) {
    throw std::runtime_error(
      "Cannot parse JSON file " + path.string() + ": " + ex.what());
  }
}

auto LoadSchema(const std::string_view relative_to_cooker) -> nlohmann::json
{
  return LoadJson(SchemaPath(relative_to_cooker));
}

auto ValidateJson(const nlohmann::json& schema, const nlohmann::json& document)
  -> std::vector<std::string>
{
  try {
    auto validator = nlohmann::json_schema::json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] auto _ = validator.validate(document, handler);
    return handler.TakeErrors();
  } catch (const std::exception& ex) {
    return { std::string("<schema>: ") + ex.what() };
  }
}

auto SchemaCaseName(const ::testing::TestParamInfo<SchemaCase>& info)
  -> std::string
{
  return std::string(info.param.name);
}

auto ExpectSchemaCase(const nlohmann::json& schema, const SchemaCase& c) -> void
{
  const auto errors = ValidateJson(schema, nlohmann::json::parse(c.json));
  if (c.valid) {
    EXPECT_THAT(errors, ::testing::IsEmpty());
  } else {
    ASSERT_FALSE(c.error_substr.empty())
      << "Rejection cases must name the expected error";
    EXPECT_THAT(errors,
      ::testing::Contains(::testing::HasSubstr(std::string(c.error_substr))));
  }
}

} // namespace oxygen::cooker::test
