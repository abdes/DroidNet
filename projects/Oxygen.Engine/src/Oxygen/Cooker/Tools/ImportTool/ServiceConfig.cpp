//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <any>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/ServiceConfig.h>

namespace oxygen::content::import::tool {

namespace {
  using oxygen::clap::CommandLineContext;
  using oxygen::content::import::ImportConcurrency;
  using oxygen::content::import::ImportPipelineConcurrency;

  auto Trim(std::string_view value) -> std::string_view
  {
    auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
      return {};
    }
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1U);
  }

  auto ParseUnsigned(std::string_view value, uint32_t& out) -> bool
  {
    uint32_t parsed = 0U;
    const auto* begin = std::to_address(value.begin());
    const auto* end = std::to_address(value.end());
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc {} || result.ptr != end) {
      return false;
    }
    if (parsed == 0U) {
      return false;
    }
    out = parsed;
    return true;
  }

  auto SelectPipeline(ImportConcurrency& concurrency, const char key)
    -> ImportPipelineConcurrency*
  {
    switch (key) {
    case 't':
      return &concurrency.texture;
    case 'b':
      return &concurrency.buffer;
    case 'm':
      return &concurrency.material;
    case 'h':
      return &concurrency.mesh_build;
    case 'g':
      return &concurrency.geometry;
    case 's':
      return &concurrency.scene;
    default:
      return nullptr;
    }
  }

  auto ParseConcurrencySpec(const std::string_view spec)
    -> std::expected<ImportConcurrency, std::error_code>
  {
    ImportConcurrency result {};
    auto remaining = Trim(spec);
    if (remaining.empty()) {
      return result;
    }

    while (!remaining.empty()) {
      const auto comma = remaining.find(',');
      auto token = Trim(remaining.substr(0, comma));
      if (!token.empty()) {
        const auto colon = token.find(':');
        if (colon == std::string_view::npos || colon == 0U) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }
        const auto lowered
          = std::tolower(static_cast<unsigned char>(token.front()));
        const char key = static_cast<char>(lowered);
        auto* pipeline = SelectPipeline(result, key);
        if (pipeline == nullptr) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }

        auto spec_value = Trim(token.substr(colon + 1U));
        if (spec_value.empty()) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }

        const auto slash = spec_value.find('/');
        const auto workers_view = Trim(spec_value.substr(0, slash));
        uint32_t workers = 0U;
        if (!ParseUnsigned(workers_view, workers)) {
          return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
        }
        pipeline->workers = workers;

        if (slash != std::string_view::npos) {
          auto capacity_view = Trim(spec_value.substr(slash + 1U));
          if (capacity_view.empty()) {
            return std::unexpected(
              std::make_error_code(std::errc::invalid_argument));
          }
          uint32_t capacity = 0U;
          if (!ParseUnsigned(capacity_view, capacity)) {
            return std::unexpected(
              std::make_error_code(std::errc::invalid_argument));
          }
          pipeline->queue_capacity = capacity;
        }
      }

      if (comma == std::string_view::npos) {
        break;
      }
      remaining = remaining.substr(comma + 1U);
    }

    return result;
  }

} // namespace

auto ApplyServiceConfigOverrides(const CommandLineContext& context,
  const GlobalOptions& global_options,
  AsyncImportService::Config& service_config, ServiceOverrides& overrides)
  -> toolkit::ToolResult
{
  overrides.thread_pool_size = context.ovm.HasOption("thread-pool-size");
  overrides.concurrency = context.ovm.HasOption("concurrency");

  if (overrides.thread_pool_size) {
    const auto& values = context.ovm.ValuesOf("thread-pool-size");
    if (!values.empty()) {
      if (const auto* size = std::any_cast<uint32_t>(&values.back().Value())) {
        service_config.thread_pool_size = *size;
      }
    }
  }

  if (!overrides.concurrency) {
    return {};
  }

  const auto& values = context.ovm.ValuesOf("concurrency");
  if (values.empty()) {
    overrides.concurrency = false;
    return {};
  }

  const auto* spec = std::any_cast<std::string>(&values.back().Value());
  const auto parsed = ParseConcurrencySpec(
    spec != nullptr ? std::string_view(*spec) : std::string_view {});
  if (!parsed.has_value()) {
    constexpr std::string_view kMessage
      = "ERROR: invalid --concurrency specification";
    if (global_options.writer != nullptr) {
      global_options.writer->Error(kMessage);
    }
    return std::unexpected(toolkit::ToolError::Usage());
  }

  service_config.concurrency = *parsed;
  return {};
}

//! Creates the command's import service, or returns the exit status of a
//! configuration error.
auto CreateImportService(ImportCommand& active_command,
  const AsyncImportService::Config& service_config,
  const ServiceOverrides& overrides)
  -> std::expected<std::unique_ptr<AsyncImportService>, toolkit::ToolError>
{
  const auto config = active_command.PrepareImportServiceConfig();
  if (!config.has_value()) {
    return std::unexpected(
      toolkit::ToolError::WithCode(ExitStatusFromError(config.error())));
  }
  auto final_config = *config;
  if (service_config.script_compile_callback
    && !final_config.script_compile_callback) {
    final_config.script_compile_callback
      = service_config.script_compile_callback;
  }
  if (overrides.thread_pool_size) {
    final_config.thread_pool_size = service_config.thread_pool_size;
  }
  if (overrides.concurrency) {
    final_config.concurrency = service_config.concurrency;
  }
  return std::make_unique<AsyncImportService>(final_config);
}

} // namespace oxygen::content::import::tool
