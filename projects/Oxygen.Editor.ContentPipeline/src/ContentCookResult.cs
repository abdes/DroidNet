// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of an explicit content cook workflow.
/// </summary>
/// <param name="OperationId">The correlated operation identity.</param>
/// <param name="TargetKind">The cooked target kind.</param>
/// <param name="Status">The reduced operation status.</param>
/// <param name="Diagnostics">The diagnostics emitted by the full cook workflow.</param>
/// <param name="CookedAssets">The cooked assets reported by the workflow.</param>
/// <param name="Inspection">The cooked-output inspection result, when available.</param>
/// <param name="Validation">The cooked-output validation result, when available.</param>
public sealed record ContentCookResult(
    Guid OperationId,
    CookTargetKind TargetKind,
    OperationStatus Status,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    IReadOnlyList<ContentCookedAsset> CookedAssets,
    CookInspectionResult? Inspection,
    CookValidationResult? Validation);
