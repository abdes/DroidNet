//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Resource.h"

#include <Oxygen/Base/Logging.h>

#include "../Integration/GraphicsLayerIntegration.h"

namespace oxygen::examples::asyncsim {

// Enhanced ResourceAliasValidator with AsyncEngine integration
class AsyncEngineResourceAliasValidator : public ResourceAliasValidator {
public:
  explicit AsyncEngineResourceAliasValidator(
    GraphicsLayerIntegration* graphics_integration) noexcept
    : graphics_integration_(graphics_integration)
  {
  }

  [[nodiscard]] auto ValidateAliasing() -> std::vector<AliasHazard> override
  {
    std::vector<AliasHazard> hazards;

    // Enhanced validation with AsyncEngine integration
    if (graphics_integration_) {
      // Validate integration state consistency
      if (!graphics_integration_->ValidateIntegrationState()) {
        AliasHazard hazard;
        hazard.description = "Graphics layer integration state is inconsistent";
        hazards.push_back(std::move(hazard));
      }

      // Check for resource lifetime conflicts with deferred reclaimer
      const auto stats = graphics_integration_->GetIntegrationStats();
      if (stats.pending_reclaims > 0) {
        LOG_F(2,
          "[ResourceValidator] {} pending resource reclaims detected "
          "during aliasing validation",
          stats.pending_reclaims);
      }
    }

    return hazards;
  }

  [[nodiscard]] auto GetDebugInfo() const -> std::string override
  {
    if (!graphics_integration_) {
      return "AsyncEngineResourceAliasValidator (no graphics integration)";
    }

    const auto stats = graphics_integration_->GetIntegrationStats();
    return "AsyncEngineResourceAliasValidator - "
           "Resources: "
      + std::to_string(stats.active_resources)
      + ", Descriptors: " + std::to_string(stats.allocated_descriptors)
      + ", Pending: " + std::to_string(stats.pending_reclaims);
  }

private:
  GraphicsLayerIntegration* graphics_integration_;
};

//! Factory function to create AsyncEngine-integrated validator
auto CreateAsyncEngineResourceValidator(GraphicsLayerIntegration* integration)
  -> std::unique_ptr<ResourceAliasValidator>
{
  return std::make_unique<AsyncEngineResourceAliasValidator>(integration);
}

} // namespace oxygen::examples::asyncsim
