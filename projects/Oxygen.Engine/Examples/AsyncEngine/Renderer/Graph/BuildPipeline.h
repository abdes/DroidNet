//===----------------------------------------------------------------------===//
// Lightweight build pipeline primitives for RenderGraphBuilder
//===----------------------------------------------------------------------===//
#pragma once

#include <expected>
#include <string>
#include <vector>

namespace oxygen::examples::asyncsim {

struct PhaseError {
  std::string message;
};

struct PhaseResult {
  std::expected<void, PhaseError> status;
};

// Forward declare build context to avoid cycles. Actual type defined in .cpp
struct BuildContext;

struct IBuildPhase {
  virtual ~IBuildPhase() = default;
  virtual PhaseResult Run(BuildContext& ctx) const = 0;
};

using PhaseList = std::vector<std::unique_ptr<IBuildPhase>>;

} // namespace oxygen::examples::asyncsim
