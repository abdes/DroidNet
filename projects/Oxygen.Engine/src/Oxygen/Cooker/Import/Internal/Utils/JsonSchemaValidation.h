//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>

namespace oxygen::content::import::internal {

//! The file the import schemas reference for the shared name definition.
inline constexpr std::string_view kNameSchemaFileName
  = "oxygen.name.schema.json";

//! The file the import schemas reference for the shared reference definition.
inline constexpr std::string_view kReferenceSchemaFileName
  = "oxygen.reference.schema.json";

//! The file the schemas reference for the shared LOD policy definition.
inline constexpr std::string_view kLodPolicySchemaFileName
  = "oxygen.lod-policy.schema.json";

//! The file the model import record references for its settings.
inline constexpr std::string_view kImportManifestSchemaFileName
  = "oxygen.import-manifest.schema.json";

//! The file the model import record references for its surface provenance.
inline constexpr std::string_view kSurfaceProvenanceSchemaFileName
  = "oxygen.surface-provenance.schema.json";

//! The file the geometry descriptor references for its buffer declarations.
inline constexpr std::string_view kBufferContainerSchemaFileName
  = "oxygen.buffer-container.schema.json";

//! A validator for an embedded import schema.
/*!
 The schema's references to `oxygen.name.schema.json`,
 `oxygen.reference.schema.json`, `oxygen.lod-policy.schema.json`,
 `oxygen.import-manifest.schema.json`, `oxygen.surface-provenance.schema.json`
 and `oxygen.buffer-container.schema.json` resolve to the embedded schemas.

 @throw std::invalid_argument when the schema references any other file.
*/
inline auto MakeJsonSchemaValidator(const nlohmann::json& schema)
  -> nlohmann::json_schema::json_validator
{
  auto validator = nlohmann::json_schema::json_validator {
    [](const nlohmann::json_uri& uri, nlohmann::json& loaded) -> void {
      const auto path = std::string_view { uri.path() };
      const auto file = path.substr(path.find_last_of('/') + 1);
      if (file == kNameSchemaFileName) {
        loaded = nlohmann::json::parse(kNameSchema);
      } else if (file == kReferenceSchemaFileName) {
        loaded = nlohmann::json::parse(kReferenceSchema);
      } else if (file == kLodPolicySchemaFileName) {
        loaded = nlohmann::json::parse(kLodPolicySchema);
      } else if (file == kImportManifestSchemaFileName) {
        loaded = nlohmann::json::parse(kImportManifestSchema);
      } else if (file == kSurfaceProvenanceSchemaFileName) {
        loaded = nlohmann::json::parse(kSurfaceProvenanceSchema);
      } else if (file == kBufferContainerSchemaFileName) {
        loaded = nlohmann::json::parse(kBufferContainerSchema);
      } else {
        throw std::invalid_argument(
          "unknown schema reference: " + uri.location());
      }
    },
  };
  validator.set_root_schema(schema);
  return validator;
}

//! One schema issue, located by the RFC 6901 pointer of the offending value
//! (empty for the document root).
struct JsonSchemaIssue final {
  std::string json_pointer;
  std::string message;
};

struct JsonSchemaValidationDiagnosticConfig final {
  std::string_view validation_failed_code;
  std::string_view validation_failed_prefix;
  std::string_view validator_failure_code;
  std::string_view validator_failure_prefix;
};

class CollectingJsonSchemaErrorHandler final
  : public nlohmann::json_schema::error_handler {
public:
  //! Records the issue; a refused string is named in its message, because a
  //! refused key has no pointer of its own and its message does not name it.
  void error(const nlohmann::json::json_pointer& ptr,
    const nlohmann::json& instance, const std::string& message) override
  {
    constexpr std::size_t kNamedLength = 128;
    auto text = message;
    if (instance.is_string()
      && instance.get_ref<const std::string&>().size() <= kNamedLength) {
      text += ": '" + instance.get<std::string>() + "'";
    }
    issues_.push_back(JsonSchemaIssue {
      .json_pointer = ptr.to_string(),
      .message = std::move(text),
    });
  }

  [[nodiscard]] auto Issues() const noexcept
    -> const std::vector<JsonSchemaIssue>&
  {
    return issues_;
  }

private:
  std::vector<JsonSchemaIssue> issues_;
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

//! Validates `instance` against `validator`'s schema.
/*!
 @throw std::invalid_argument naming `what` and the JSON pointer and message
 of every issue; never the document, which can be large.
*/
inline auto ValidateJsonSchema(nlohmann::json_schema::json_validator& validator,
  const nlohmann::json& instance, const std::string_view what) -> void
{
  auto issues = std::vector<JsonSchemaIssue> {};
  auto internal_error = std::string {};
  if (!CollectJsonSchemaIssues(validator, instance, issues, internal_error)) {
    throw std::invalid_argument(
      std::string(what) + " could not be validated: " + internal_error);
  }
  if (issues.empty()) {
    return;
  }
  auto message = std::string(what) + " is invalid";
  auto separator = std::string_view { ": " };
  for (const auto& issue : issues) {
    message.append(separator);
    message.append(issue.json_pointer.empty() ? "(root)" : issue.json_pointer);
    message.append(": ");
    message.append(issue.message);
    separator = "; ";
  }
  throw std::invalid_argument(message);
}

//! Validates `instance` and emits every schema issue as `emit(code, message,
//! json_pointer)`; true when the schema accepts the instance.
template <typename EmitDiagnosticFn>
inline auto ValidateJsonSchemaWithDiagnostics(
  nlohmann::json_schema::json_validator& validator,
  const nlohmann::json& instance,
  const JsonSchemaValidationDiagnosticConfig& config,
  const EmitDiagnosticFn& emit) -> bool
{
  auto issues = std::vector<JsonSchemaIssue> {};
  auto internal_error = std::string {};
  if (!CollectJsonSchemaIssues(validator, instance, issues, internal_error)) {
    emit(config.validator_failure_code,
      std::string(config.validator_failure_prefix) + internal_error,
      std::string {});
    return false;
  }
  for (const auto& issue : issues) {
    emit(config.validation_failed_code,
      std::string(config.validation_failed_prefix) + issue.message,
      issue.json_pointer);
  }
  return issues.empty();
}

} // namespace oxygen::content::import::internal
