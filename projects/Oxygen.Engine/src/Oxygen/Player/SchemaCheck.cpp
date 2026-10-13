//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Player/Internal/PlayerSchemas.h>
#include <Oxygen/Player/SchemaCheck.h>

namespace oxygen::player {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  class CollectingErrorHandler final
    : public nlohmann::json_schema::error_handler {
  public:
    auto error(const json::json_pointer& pointer, const json& instance,
      const std::string& message) -> void override
    {
      auto path = pointer.to_string();
      auto text = (path.empty() ? std::string("<root>") : std::move(path))
        + ": " + message;
      if (!instance.is_discarded() && instance.is_primitive()) {
        text += " (value " + instance.dump() + ")";
      }
      errors_.push_back(std::move(text));
    }

    [[nodiscard]] auto TakeErrors() -> std::vector<std::string>
    {
      return std::move(errors_);
    }

  private:
    std::vector<std::string> errors_;
  };

} // namespace

SchemaCheck::SchemaCheck(const std::string_view schema)
  : validator_(std::make_unique<json_validator>())
{
  validator_->set_root_schema(json::parse(schema));
}

SchemaCheck::~SchemaCheck() = default;

auto SchemaCheck::Validate(const json& value) const -> std::vector<std::string>
{
  auto handler = CollectingErrorHandler {};
  [[maybe_unused]] const auto patch = validator_->validate(value, handler);
  return handler.TakeErrors();
}

auto LaunchSchema() -> const SchemaCheck&
{
  static const auto check = SchemaCheck { kPlayerLaunchSchema };
  return check;
}

auto EventSchema() -> const SchemaCheck&
{
  static const auto check = SchemaCheck { kPlayerEventsSchema };
  return check;
}

} // namespace oxygen::player
