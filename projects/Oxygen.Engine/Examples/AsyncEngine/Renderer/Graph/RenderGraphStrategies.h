//===----------------------------------------------------------------------===//
// Lightweight strategy interfaces for render graph build-time optimizations
// and analysis. These allow pluggable behaviors (promotion, scheduling
// tweaks, alias packers) to be registered with the builder.
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <vector>

#include "Validator.h" // for ValidationError

namespace oxygen::engine::asyncsim {

struct BuildContext; // forward (defined in RenderGraphBuilder.h)

// Simple diagnostics sink used by strategies to report issues. For now this
// is a minimal wrapper that logs via validation error objects; later it can
// be extended to capture structured diagnostics for tests.
struct DiagnosticsSink {
  // Called by strategies to report an error
  virtual void AddError(const ValidationError& err) = 0;
  // Called by strategies to report a warning
  virtual void AddWarning(const ValidationError& w) = 0;
  virtual ~DiagnosticsSink() = default;
};

// Graph-level optimization strategy interface
struct IGraphOptimization {
  virtual ~IGraphOptimization() = default;
  virtual void apply(BuildContext& ctx, DiagnosticsSink& sink) = 0;
};

// Analysis pass interface (for heavier analyses returning results)
struct AnalysisResults {
  // placeholder for analysis outputs
};

struct IAnalysisPass {
  virtual ~IAnalysisPass() = default;
  virtual void run(BuildContext& ctx, AnalysisResults& out) = 0;
};

} // namespace oxygen::engine::asyncsim
