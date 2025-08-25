//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../FrameContext.h"
#include "../Passes/RenderPass.h"
#include "Resource.h"
#include "Types.h"

// Forward declarations for AsyncEngine integration
namespace oxygen::examples::asyncsim {
class FrameContext;
}

namespace oxygen::examples::asyncsim {

// Forward declarations
class RenderGraphBuilder;
class RenderGraph;

//! Types of validation errors
enum class ValidationErrorType : uint32_t {
  // Dependency errors
  CircularDependency, //!< Circular dependency detected in pass graph
  MissingDependency, //!< Required dependency not found
  InvalidDependencyOrder, //!< Dependency order violates constraints

  // Resource errors
  ResourceNotFound, //!< Referenced resource does not exist
  InvalidResourceState, //!< Resource state transition is invalid
  ResourceLifetimeViolation, //!< Resource accessed outside its lifetime
  ResourceAliasHazard, //!< Dangerous resource aliasing detected

  // View errors
  ViewScopeViolation, //!< Pass scope doesn't match view configuration
  ViewInfoMissing, //!< Required view context not provided

  // Performance warnings
  SuboptimalScheduling, //!< Scheduling could be improved
  MemoryPressure, //!< High memory usage detected

  // Generic errors
  InvalidConfiguration, //!< General configuration error
  InternalError //!< Internal validation error
};

//! Validation error information
struct ValidationError {
  ValidationErrorType type;
  std::string message;
  std::vector<PassHandle> affected_passes;
  std::vector<ResourceHandle> affected_resources;

  ValidationError() = default;

  ValidationError(ValidationErrorType err_type, std::string msg)
    : type(err_type)
    , message(std::move(msg))
  {
  }

  //! Get severity level of this error
  [[nodiscard]] auto GetSeverity() const -> std::string
  {
    switch (type) {
    case ValidationErrorType::CircularDependency:
    case ValidationErrorType::ResourceAliasHazard:
    case ValidationErrorType::InternalError:
      return "Error";
    case ValidationErrorType::MissingDependency:
    case ValidationErrorType::InvalidResourceState:
    case ValidationErrorType::ResourceLifetimeViolation:
      return "Error";
    case ValidationErrorType::SuboptimalScheduling:
    case ValidationErrorType::MemoryPressure:
      return "Warning";
    default:
      return "Info";
    }
  }
};

//! Validation result summary
struct ValidationResult {
  bool is_valid { true };
  std::vector<ValidationError> errors;
  std::vector<ValidationError> warnings;
  std::string summary;

  ValidationResult() = default;

  //! Add an error to the result
  auto AddError(const ValidationError& error) -> void
  {
    if (error.GetSeverity() == "Error") {
      errors.push_back(error);
      is_valid = false;
    } else if (error.GetSeverity() == "Warning") {
      warnings.push_back(error);
    }
  }

  //! Add a warning to the result
  auto AddWarning(const ValidationError& warning) -> void
  {
    warnings.push_back(warning);
  }

  //! Check if validation passed
  [[nodiscard]] auto IsValid() const -> bool { return is_valid; }

  //! Get total error count
  [[nodiscard]] auto GetErrorCount() const -> size_t { return errors.size(); }

  //! Get total warning count
  [[nodiscard]] auto GetWarningCount() const -> size_t
  {
    return warnings.size();
  }
};

//! Interface for render graph validation
/*!
 Provides comprehensive validation of render graph structure, resource usage,
 dependencies, and view configuration. Reports detailed error information
 with actionable feedback.

 Enhanced with AsyncEngine integration for proper error reporting through
 the engine's logging and error handling systems.
 */
class RenderGraphValidator {
public:
  RenderGraphValidator() = default;
  virtual ~RenderGraphValidator() = default;

  // Non-copyable, movable
  RenderGraphValidator(const RenderGraphValidator&) = delete;
  auto operator=(const RenderGraphValidator&) -> RenderGraphValidator& = delete;
  RenderGraphValidator(RenderGraphValidator&&) = default;
  auto operator=(RenderGraphValidator&&) -> RenderGraphValidator& = default;

  // === ASYNCENGINE INTEGRATION ===

  //! Set module context for AsyncEngine error reporting integration
  auto SetModuleContext(FrameContext* module_context) -> void
  {
    module_context_ = module_context;
  }

  //! Check if AsyncEngine integration is available
  [[nodiscard]] auto HasEngineIntegration() const -> bool
  {
    return module_context_ != nullptr;
  }

  //! Get current frame index for error context
  [[nodiscard]] auto GetCurrentFrameIndex() const -> uint64_t
  {
    return module_context_ ? module_context_->GetFrameIndex() : 0;
  }

  //! Validate complete render graph
  [[nodiscard]] virtual auto ValidateGraph(
    const RenderGraphBuilder& /*builder*/) -> ValidationResult
  {
    ValidationResult result;

    // Phase 1 stub implementation
    result.summary
      = "RenderGraphValidator (stub implementation) - validation passed";

    return result;
  }

  //! Validate pass dependencies
  [[nodiscard]] virtual auto ValidateDependencies(
    const std::vector<PassHandle>& passes) -> ValidationResult
  {
    ValidationResult result;

    // Stub implementation - Phase 1
    if (passes.empty()) {
      result.AddError(ValidationError {
        ValidationErrorType::InvalidConfiguration, "No passes to validate" });
    }

    return result;
  }

  //! Validate resource usage and states
  [[nodiscard]] virtual auto ValidateResourceUsage(
    const std::unordered_map<ResourceHandle, std::unique_ptr<ResourceDesc>>&
      resources) -> ValidationResult
  {
    ValidationResult result;

    // Stub implementation - Phase 1
    (void)resources;

    return result;
  }

  //! Validate view configuration
  [[nodiscard]] virtual auto ValidateViews(const FrameContext& frame_context)
    -> ValidationResult
  {
    ValidationResult result;

    // Basic validation
    if (frame_context.GetViews().empty()) {
      result.AddError(ValidationError { ValidationErrorType::ViewInfoMissing,
        "No views configured forrendering" });
    }

    return result;
  }

  //! Detect circular dependencies in pass graph
  [[nodiscard]] virtual auto DetectCircularDependencies(
    const std::vector<PassHandle>& passes) -> std::vector<PassHandle>
  {
    // Stub implementation - Phase 1
    (void)passes;
    return {};
  }

  //! Validate resource state transitions
  [[nodiscard]] virtual auto ValidateStateTransitions(
    ResourceHandle resource, const std::vector<ResourceState>& states) -> bool
  {
    // Stub implementation - Phase 1
    (void)resource;
    (void)states;
    return true;
  }

  //! Check for resource hazards
  [[nodiscard]] virtual auto CheckResourceHazards(
    const std::vector<ResourceHandle>& resources)
    -> std::vector<ValidationError>
  {
    // Stub implementation - Phase 1
    (void)resources;
    return {};
  }

  //! Validate pass scheduling order
  [[nodiscard]] virtual auto ValidateSchedulingOrder(
    const std::vector<PassHandle>& execution_order) -> ValidationResult
  {
    ValidationResult result;

    // Basic validation
    if (execution_order.empty()) {
      result.AddError(ValidationError {
        ValidationErrorType::InvalidConfiguration, "Empty scheduling order" });
    }

    return result;
  }

  //! Enable or disable strict validation
  virtual auto SetStrictValidation(bool enabled) -> void
  {
    strict_validation_enabled_ = enabled;
  }

  //! Enable or disable performance warnings
  virtual auto SetPerformanceWarnings(bool enabled) -> void
  {
    performance_warnings_enabled_ = enabled;
  }

  //! Set memory pressure threshold for warnings
  virtual auto SetMemoryPressureThreshold(size_t bytes) -> void
  {
    memory_pressure_threshold_ = bytes;
  }

  //! Get comprehensive validation report
  [[nodiscard]] virtual auto GenerateReport(
    const ValidationResult& result) const -> std::string
  {
    std::string report = "=== Render Graph Validation Report ===\n";

    // Add frame context if available
    if (HasEngineIntegration()) {
      report += "Frame: " + std::to_string(GetCurrentFrameIndex()) + "\n";
    }

    report += "Status: ";
    report += (result.IsValid() ? "VALID" : "INVALID");
    report += "\n";
    report += "Errors: " + std::to_string(result.GetErrorCount()) + "\n";
    report += "Warnings: " + std::to_string(result.GetWarningCount()) + "\n";

    if (!result.errors.empty()) {
      report += "\nErrors:\n";
      for (const auto& error : result.errors) {
        report += "- " + error.message + "\n";
      }
    }

    if (!result.warnings.empty()) {
      report += "\nWarnings:\n";
      for (const auto& warning : result.warnings) {
        report += "- " + warning.message + "\n";
      }
    }

    return report;
  }

  //! Get debug information
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "RenderGraphValidator (stub implementation)";
  }

protected:
  // Configuration
  bool strict_validation_enabled_ { true };
  bool performance_warnings_enabled_ { true };
  size_t memory_pressure_threshold_ { 1024 * 1024 * 1024 }; // 1GB

  // AsyncEngine integration
  FrameContext* module_context_ { nullptr };
};

//! AsyncEngine-specific render graph validator
/*!
 Enhanced validator with AsyncEngine integration for cross-module validation,
 graphics layer compatibility checking, and performance optimization.
 */
class AsyncRenderGraphValidator : public RenderGraphValidator {
public:
  AsyncRenderGraphValidator() = default;

  [[nodiscard]] auto ValidateGraph(const RenderGraphBuilder& builder)
    -> ValidationResult override;
};

//! Factory function to create AsyncEngine validator
// Unified factory returning base interface pointer
auto CreateAsyncRenderGraphValidator() -> std::unique_ptr<RenderGraphValidator>;

} // namespace oxygen::examples::asyncsim
