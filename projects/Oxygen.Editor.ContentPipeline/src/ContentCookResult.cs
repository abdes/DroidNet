// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;

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
    CookValidationResult? Validation)
{
    /// <summary>Gets the saved inputs and compatible producer fingerprint used by this cook.</summary>
    public CookInputSnapshot? InputSnapshot { get; init; }

    /// <summary>Gets whether captured inputs still match current authoring at completion, when checked.</summary>
    public bool? InputsAreCurrent { get; init; }

    /// <summary>Gets validated existing products reused without native import.</summary>
    public IReadOnlyList<ContentCookedAsset> ReusedAssets { get; init; } = [];

    /// <summary>Gets a value indicating whether every requested product was already current.</summary>
    public bool IsUpToDate { get; init; }

    /// <summary>Gets a value indicating whether roots and metadata committed as a journaled publication.</summary>
    public bool IsPublished { get; init; }

    /// <summary>Gets a value indicating whether the committed content was applied to a running preview.</summary>
    public bool IsMounted { get; init; }

    /// <summary>Gets the output evidence captured under the native validation read lease.</summary>
    internal Incremental.CookProvenance.Root? VerifiedRoot { get; init; }
}
