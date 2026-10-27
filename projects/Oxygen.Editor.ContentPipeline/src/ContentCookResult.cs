// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
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
    /// <summary>Gets the retained source that remains available after an import, including failed cooking.</summary>
    public AssetPath? RetainedSourceUri { get; init; }

    /// <summary>Gets the saved inputs and compatible producer fingerprint used by this cook.</summary>
    public CookInputSnapshot? InputSnapshot { get; init; }

    /// <summary>Gets whether captured inputs still match current authoring at completion, when checked.</summary>
    public bool? InputsAreCurrent { get; init; }

    /// <summary>Gets validated existing products reused without native import.</summary>
    public IReadOnlyList<ContentCookedAsset> ReusedAssets { get; init; } = [];

    /// <summary>
    /// Gets the identities of the <see cref="Diagnostics"/> that still apply to reused products. The earlier cook that
    /// made those products already reported them.
    /// </summary>
    public ImmutableHashSet<Guid> ReusedDiagnosticIds { get; init; } = [];

    /// <summary>
    /// Gets what this cook reports: its <see cref="Diagnostics"/> without those of reused products, which the earlier cook
    /// that made them already reported.
    /// </summary>
    public IReadOnlyList<DiagnosticRecord> ReportedDiagnostics
        => [.. this.Diagnostics.Where(issue => !this.ReusedDiagnosticIds.Contains(issue.DiagnosticId))];

    /// <summary>
    /// Gets the status of the work this cook did: one whose only warnings belong to reused products succeeded.
    /// </summary>
    public OperationStatus ReportedStatus
        => this.Status == OperationStatus.SucceededWithWarnings
            && !this.ReportedDiagnostics.Any(static issue => issue.Severity == DiagnosticSeverity.Warning)
            ? OperationStatus.Succeeded
            : this.Status;

    /// <summary>Gets a value indicating whether every requested product was already current.</summary>
    public bool IsUpToDate { get; init; }

    /// <summary>Gets a value indicating whether roots and metadata committed as a journaled publication.</summary>
    public bool IsPublished { get; init; }

    /// <summary>Gets a value indicating whether the committed content was applied to a running preview.</summary>
    public bool IsMounted { get; init; }

    /// <summary>Gets the output evidence captured under the native validation read lease.</summary>
    internal Incremental.CookProvenance.Root? VerifiedRoot { get; init; }

    /// <summary>Gets native candidate metadata used to preserve unrelated descriptor identities.</summary>
    internal global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport? NativeInventory { get; init; }

    /// <summary>Gets native source-allocation candidates awaiting journaled publication.</summary>
    internal ImmutableDictionary<string, System.Text.Json.JsonElement> ModelImports { get; init; }
        = [];

    /// <summary>Gets native-reported auxiliary file ownership for imported source products.</summary>
    internal ImmutableDictionary<AssetPath, ImmutableArray<string>> AuxiliaryFilesBySource { get; init; }
        = [];

    /// <summary>Gets model import records emitted by the native producer and committed with its outputs.</summary>
    internal ImmutableArray<Publication.CookProducedSourceFile> ProducedSourceFiles { get; init; } = [];
}
