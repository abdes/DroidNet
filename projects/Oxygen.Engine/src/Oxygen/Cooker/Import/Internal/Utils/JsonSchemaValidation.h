//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

namespace oxygen::content::import::internal {

struct JsonSchemaIssue final {
  std::string object_path;
  std::string message;
};

struct JsonSchemaValidationDiagnosticConfig final {
  std::string_view validation_failed_code;
  std::string_view validation_failed_prefix;
  std::string_view validation_overflow_prefix;
  std::string_view validator_failure_code;
  std::string_view validator_failure_prefix;
  size_t max_issues = 12;
};

class CollectingJsonSchemaErrorHandler final
  : public nlohmann::json_schema::error_handler {
public:
  void error(const nlohmann::json::json_pointer& ptr, const nlohmann::json&,
    const std::string& message) override
  {
    issues_.push_back(JsonSchemaIssue {
      .object_path = JsonPointerToObjectPath(ptr.to_string()),
      .message = message,
    });
  }

  [[nodiscard]] auto Issues() const noexcept
    -> const std::vector<JsonSchemaIssue>&
  {
    return issues_;
  }

private:
  static auto DecodeJsonPointerToken(std::string_view token) -> std::string
  {
    auto out = std::string {};
    out.reserve(token.size());
    for (size_t i = 0; i < token.size(); ++i) {
      if (token[i] == '~' && i + 1 < token.size()) {
        if (token[i + 1] == '0') {
          out.push_back('~');
          ++i;
          continue;
        }
        if (token[i + 1] == '1') {
          out.push_back('/');
          ++i;
          continue;
        }
      }
      out.push_back(token[i]);
    }
    return out;
  }

  static auto IsArrayIndexToken(std::string_view token) -> bool
  {
    if (token.empty()) {
      return false;
    }
    return std::ranges::all_of(token, [](const char ch) {
      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    });
  }

  static auto JsonPointerToObjectPath(std::string_view pointer) -> std::string
  {
    if (pointer.empty() || pointer == "/") {
      return {};
    }

    auto out = std::string {};
    if (pointer.front() == '/') {
      pointer.remove_prefix(1);
    }

    size_t pos = 0;
    while (pos <= pointer.size()) {
      const auto next = pointer.find('/', pos);
      const auto token = next == std::string_view::npos
        ? pointer.substr(pos)
        : pointer.substr(pos, next - pos);
      const auto decoded = DecodeJsonPointerToken(token);
      if (IsArrayIndexToken(decoded)) {
        out.append("[");
        out.append(decoded);
        out.append("]");
      } else {
        if (!out.empty()) {
          out.push_back('.');
        }
        out.append(decoded);
      }
      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
    return out;
  }

  std::vector<JsonSchemaIssue> issues_ {};
};

inline auto CollectJsonSchemaIssues(
  nlohmann::json_schema::json_validator& validator,
  const nlohmann::json& instance, std::vector<JsonSchemaIssue>& out_issues,
  std::string& internal_error) -> bool
{
  internal_error.clear();
  out_issues.clear();
  auto handler = CollectingJsonSchemaErrorHandler {};

  try {
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
  } catch (const std::exception& ex) {
    internal_error = ex.what();
    return false;
  }

  out_issues = handler.Issues();
  return true;
}

template <typename EmitDiagnosticFn>
inline auto EmitCollectedJsonSchemaIssues(
  const std::vector<JsonSchemaIssue>& issues, const std::string_view code,
  const std::string_view message_prefix, const std::string_view overflow_prefix,
  const size_t max_issues, EmitDiagnosticFn&& emit) -> void
{
  const auto reported = std::min(issues.size(), max_issues);
  for (size_t i = 0; i < reported; ++i) {
    const auto& issue = issues[i];
    emit(code, std::string(message_prefix) + issue.message, issue.object_path);
  }
  if (issues.size() > reported) {
    emit(code,
      std::string(overflow_prefix) + std::to_string(issues.size())
        + " error(s); showing first " + std::to_string(reported),
      std::string {});
  }
}

template <typename EmitDiagnosticFn>
inline auto ValidateJsonSchemaWithDiagnostics(
  nlohmann::json_schema::json_validator& validator,
  const nlohmann::json& instance,
  const JsonSchemaValidationDiagnosticConfig& config, EmitDiagnosticFn&& emit)
  -> bool
{
  auto issues = std::vector<JsonSchemaIssue> {};
  auto internal_error = std::string {};
  if (!CollectJsonSchemaIssues(validator, instance, issues, internal_error)) {
    emit(config.validator_failure_code,
      std::string(config.validator_failure_prefix) + internal_error,
      std::string {});
    return false;
  }
  if (issues.empty()) {
    return true;
  }

  EmitCollectedJsonSchemaIssues(issues, config.validation_failed_code,
    config.validation_failed_prefix, config.validation_overflow_prefix,
    config.max_issues, std::forward<EmitDiagnosticFn>(emit));
  return false;
}

} // namespace oxygen::content::import::internal
