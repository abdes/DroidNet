//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Validator.h"

#include <Oxygen/Base/Logging.h>

#include "../../ModuleContext.h"
#include "RenderGraphBuilder.h"

namespace oxygen::examples::asyncsim {

//! Enhanced RenderGraphValidator with AsyncEngine integration
class AsyncEngineRenderGraphValidator : public RenderGraphValidator {
public:
  AsyncEngineRenderGraphValidator() = default;

  [[nodiscard]] auto ValidateGraph(const RenderGraphBuilder& builder)
    -> ValidationResult override
  {
    ValidationResult result;

    LOG_F(2, "[RenderGraphValidator] Validating render graph for frame {}",
      GetCurrentFrameIndex());

    // Enhanced validation with AsyncEngine context
    if (HasEngineIntegration()) {
      // Validate AsyncEngine-specific requirements
      if (!builder.HasAsyncEngineIntegration()) {
        result.AddError(
          ValidationError { ValidationErrorType::InvalidConfiguration,
            "Render graph builder missing AsyncEngine integration" });
      }

      // Check thread safety requirements
      if (builder.IsThreadSafe() && !ValidateThreadSafety(builder)) {
        result.AddError(
          ValidationError { ValidationErrorType::InvalidConfiguration,
            "Thread safety requirements not met for parallel work phase" });
      }
    }

    // Validate resources
    auto resource_result
      = ValidateResourceUsage(builder.GetResourceDescriptors());
    for (const auto& error : resource_result.errors) {
      result.AddError(error);
    }
    for (const auto& warning : resource_result.warnings) {
      result.AddWarning(warning);
    }

    // Validate frame context
    auto frame_result = ValidateMultiView(builder.GetFrameContext());
    for (const auto& error : frame_result.errors) {
      result.AddError(error);
    }

    // Log validation results using AsyncEngine logging
    if (result.IsValid()) {
      LOG_F(2, "[RenderGraphValidator] Validation passed - {} warnings",
        result.GetWarningCount());
    } else {
      LOG_F(ERROR,
        "[RenderGraphValidator] Validation failed - {} errors, {} warnings",
        result.GetErrorCount(), result.GetWarningCount());

      // Log each error for debugging
      for (const auto& error : result.errors) {
        LOG_F(ERROR, "[RenderGraphValidator] Error: {}", error.message);
      }
    }

    result.summary = "AsyncEngine-integrated validation - "
      + std::string(result.IsValid() ? "PASSED" : "FAILED");

    return result;
  }

  [[nodiscard]] auto ValidateDependencies(const std::vector<PassHandle>& passes)
    -> ValidationResult override
  {
    ValidationResult result;

    LOG_F(3, "[RenderGraphValidator] Validating dependencies for {} passes",
      passes.size());

    // Enhanced dependency validation
    if (passes.empty()) {
      result.AddError(
        ValidationError { ValidationErrorType::InvalidConfiguration,
          "No passes to validate for frame "
            + std::to_string(GetCurrentFrameIndex()) });
      return result;
    }

    // Check for circular dependencies
    auto circular_deps = DetectCircularDependencies(passes);
    if (!circular_deps.empty()) {
      std::string error_msg
        = "Circular dependencies detected involving passes: ";
      for (const auto& pass : circular_deps) {
        error_msg += std::to_string(pass.get()) + " ";
      }

      result.AddError(
        ValidationError { ValidationErrorType::CircularDependency, error_msg });

      // Log detailed dependency error
      LOG_F(ERROR, "[RenderGraphValidator] {}", error_msg);
    }

    return result;
  }

private:
  //! Validate thread safety requirements for parallel execution
  [[nodiscard]] auto ValidateThreadSafety(
    const RenderGraphBuilder& builder) const -> bool
  {
    // Check if builder is properly configured for thread-safe operation
    if (!builder.HasAsyncEngineIntegration()) {
      LOG_F(WARNING,
        "[RenderGraphValidator] Thread safety requires AsyncEngine "
        "integration");
      return false;
    }

    LOG_F(3, "[RenderGraphValidator] Thread safety validation passed");
    return true;
  }

  //! Get resource descriptors from builder (helper method)
  [[nodiscard]] auto GetResourceDescriptors(
    const RenderGraphBuilder& builder) const
    -> const std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>&
  {
    // In a real implementation, would access builder's private members
    // For now, return empty map as stub
    static const std::unordered_map<ResourceHandle,
      std::unique_ptr<ResourceDesc>>
      empty_map;
    return empty_map;
  }
};

//! Factory function to create AsyncEngine-integrated validator
auto CreateAsyncEngineValidator() -> std::unique_ptr<RenderGraphValidator>
{
  return std::make_unique<AsyncEngineRenderGraphValidator>();
}

} // namespace oxygen::examples::asyncsim
