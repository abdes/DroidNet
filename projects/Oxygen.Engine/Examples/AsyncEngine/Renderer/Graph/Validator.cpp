//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Simplified, corrected implementation after cleanup.
#include "Validator.h"

#include <Oxygen/Base/Logging.h>

#include "RenderGraphBuilder.h"

namespace oxygen::examples::asyncsim {

auto AsyncRenderGraphValidator::ValidateGraph(const RenderGraphBuilder& builder)
  -> ValidationResult
{
  ValidationResult result;

  LOG_F(2, "[RenderGraphValidator] Validating render graph (frame {})",
    GetCurrentFrameIndex());

  // Simple sanity warnings
  if (builder.GetPassHandles().empty()) {
    result.AddWarning(
      ValidationError { ValidationErrorType::InvalidConfiguration,
        "Render graph has no passes" });
  }
  if (builder.GetResourceHandles().empty()) {
    result.AddWarning(
      ValidationError { ValidationErrorType::InvalidConfiguration,
        "Render graph has no resources" });
  }

  result.summary = "AsyncRenderGraphValidator stub - "
    + std::string(result.IsValid() ? "PASSED" : "FAILED");
  return result;
}

auto CreateAsyncRenderGraphValidator() -> std::unique_ptr<RenderGraphValidator>
{
  return std::make_unique<AsyncRenderGraphValidator>();
}

} // namespace oxygen::examples::asyncsim
