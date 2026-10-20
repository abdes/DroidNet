// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Routes saved-source and preview demand through the same project-owned transaction.</summary>
public sealed partial class ContentPipelineService
{
    /// <inheritdoc />
    public Task<ContentCookResult> CookSavedAssetAsync(AssetPath assetUri, ProjectContext expectedProject, CancellationToken cancellationToken)
        => this.CookAutomaticAssetAsync(assetUri, expectedProject, isDemand: false, cancellationToken);

    /// <inheritdoc />
    public Task<ContentCookResult> CookPreviewAssetAsync(AssetPath assetUri, ProjectContext expectedProject, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        return GetAssetKind(assetUri) is ContentCookAssetKind.Geometry or ContentCookAssetKind.Material
            ? this.CookAutomaticAssetAsync(assetUri, expectedProject, isDemand: true, cancellationToken)
            : throw new ArgumentException("Preview demand requires an authored geometry or material asset.", nameof(assetUri));
    }

    /// <inheritdoc />
    public Task<ContentCookResult> CookChangedSourcesAsync(CookChanges changes, ProjectContext expectedProject, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(changes);
        ArgumentNullException.ThrowIfNull(expectedProject);

        // Changed files name their sources; the cook, its run and its results name the assets they stand for.
        AssetPath[] assets = [.. changes.Sources.Select(AssetFileTypes.ToIdentity).Distinct()];
        if (assets.Length == 0)
        {
            throw new ArgumentException("A cook on change needs a changed source to cook.", nameof(changes));
        }

        var request = assets is [var only]
            ? new CookRunRequest(CookTargetKind.Asset, only, IsAutomatic: true) { CoalescePending = true }
            : new CookRunRequest(CookTargetKind.Changes, ScopeUri: null, IsAutomatic: true) { Assets = assets };
        return this.cookCoordinator.RunCookAsync(
            request with { OriginContext = expectedProject },
            (operation, token) =>
            {
                if (!ReferenceEquals(operation.Project, expectedProject))
                {
                    throw new OperationCanceledException("The changed sources' project is no longer active.", token);
                }

                ReportSkippedSources(operation, changes.Skipped);
                return this.CookAssetsCoreAsync(operation, assets, isAutomatic: true, token);
            },
            cancellationToken);
    }

    // Each changed source left out for its errors is named in an Info diagnostic of the run that cooks the others.
    private static void ReportSkippedSources(ContentCookOperation operation, IReadOnlyList<SkippedCookSource> skipped)
    {
        foreach (var source in skipped)
        {
            var errors = source.ErrorCount == 1 ? "1 error" : string.Create(CultureInfo.CurrentCulture, $"{source.ErrorCount} errors");
            var cause = source.Dependencies.Count == 0 ? $"it has {errors}"
                : $"it uses {string.Join(", ", source.Dependencies.Select(static used => $"'{used.Name}'"))}, which {(source.Dependencies.Count == 1 ? "has" : "have")} {errors}";
            var skip = new DiagnosticRecord
            {
                OperationId = operation.OperationId,
                Domain = FailureDomain.AssetCook,
                Severity = DiagnosticSeverity.Info,
                Code = SkippedForProblemsCode,
                Message = $"'{source.Source.Name}' was not cooked: {cause}, listed in Problems. The change that fixes them cooks it.",
                AffectedVirtualPath = AssetFileTypes.ToIdentity(source.Source).VirtualPath,
                AffectedPath = operation.Project.Find(source.Source)?.File.FullPath,
            };
            CookRunContext.Report(new(skip.Message, skip.Severity, Diagnostic: skip));
        }
    }

    private Task<ContentCookResult> CookAutomaticAssetAsync(AssetPath assetUri, ProjectContext expectedProject, bool isDemand, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        ArgumentNullException.ThrowIfNull(expectedProject);

        // A saved or removed file names its source; the cook, its run and its results name the asset it stands for.
        assetUri = AssetFileTypes.ToIdentity(assetUri);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, assetUri, IsAutomatic: true)
            {
                CoalescePending = true,
                IsDemand = isDemand,
                OriginContext = expectedProject,
            },
            (operation, token) => ReferenceEquals(operation.Project, expectedProject)
                ? this.CookAssetCoreAsync(operation, assetUri, token, allowImportedSourceChanges: false, isAutomatic: true)
                : throw new OperationCanceledException("The saved source's project is no longer active.", token),
            cancellationToken);
    }
}
