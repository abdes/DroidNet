// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Immutable session state of a cook, safe to project onto a UI dispatcher.</summary>
public sealed record CookRunSnapshot
{
    /// <summary>Gets the operation identity, allocated before the request queues.</summary>
    public required Guid OperationId { get; init; }

    /// <summary>Gets the owning project's identity.</summary>
    public required Guid ProjectId { get; init; }

    /// <summary>Gets the owning project's root.</summary>
    public required string ProjectRoot { get; init; }

    /// <summary>Gets the user-visible scope name.</summary>
    public required string DisplayName { get; init; }

    /// <summary>Gets the logical request used by Retry, updated after source retention.</summary>
    public required CookRunRequest Request { get; init; }

    /// <summary>Gets the monotonic presentation revision.</summary>
    public long Revision { get; init; }

    /// <summary>Gets the time the request entered the queue.</summary>
    public DateTimeOffset StartedAt { get; init; } = DateTimeOffset.UtcNow;

    /// <summary>Gets the time the operation reached its final outcome.</summary>
    public DateTimeOffset? CompletedAt { get; init; }

    /// <summary>Gets the current execution state.</summary>
    public CookRunState State { get; init; } = CookRunState.Queued;

    /// <summary>Gets this run's ordered messages, retained independently of global logs.</summary>
    public ImmutableList<CookRunMessage> Messages { get; init; } = [];

    /// <summary>Gets identified assets and their individual outcomes.</summary>
    public ImmutableDictionary<Uri, CookRunAsset> Assets { get; init; } = ImmutableDictionary<Uri, CookRunAsset>.Empty;

    /// <summary>Gets actionable structured issues from the cook.</summary>
    public ImmutableArray<DiagnosticRecord> Diagnostics { get; init; } = [];

    /// <summary>Gets the exact unsaved documents preventing input capture.</summary>
    public ImmutableArray<CookDocumentState> UnsavedDocuments { get; init; } = [];

    /// <summary>Gets a value indicating whether this run reached a final outcome.</summary>
    public bool IsCompleted => this.CompletedAt.HasValue;

    /// <summary>Gets current native output identities offered for explicit navigation after successful model cooking.</summary>
    public ImmutableArray<Uri> ImportedOutputs => !this.IsCompleted
        || this.State is not (CookRunState.Succeeded or CookRunState.SucceededWithWarnings or CookRunState.UpToDate)
        || !this.Assets.Values.Any(static asset => asset.Kind == ContentCookAssetKind.ForeignSource) ? []
        :
        [
            .. this.Assets.Values.Where(static asset => asset.State is CookAssetState.Updated or CookAssetState.Reused
            && Path.GetExtension(asset.AssetUri.AbsolutePath).ToUpperInvariant() is ".OGEO" or ".OMAT" or ".OSCENE")
            .Select(static asset => asset.AssetUri).OrderBy(static uri => uri.AbsoluteUri, StringComparer.Ordinal),
        ];
}
