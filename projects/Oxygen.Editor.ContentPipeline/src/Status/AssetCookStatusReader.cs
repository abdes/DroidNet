// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Reads the same input fingerprints and output evidence used by incremental cooking.</summary>
/// <param name="documents">Document owners protecting acknowledged saved bytes.</param>
/// <param name="publication">The committed metadata verifier.</param>
/// <param name="nativeCompatibility">The current cooking producer identity.</param>
/// <param name="files">The atomic provenance reader.</param>
public sealed class AssetCookStatusReader(
    ICookDocumentRegistry documents,
    CookPublicationService publication,
    INativeCompatibilityService nativeCompatibility,
    IAtomicFileStore files) : IAssetCookStatusReader
{
    private readonly CookProvenanceStore provenance = new(files);

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(assetUris);
        cancellationToken.ThrowIfCancellationRequested();
        if (assetUris.Count == 0)
        {
            return [];
        }

        using var output = await CookOutputLease.AcquireInspectionAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        var (prior, version) = await this.provenance.ReadAsync(project, cancellationToken).ConfigureAwait(false);
        var trusted = await publication.HasCommittedMetadataUnderLeaseAsync(project, cancellationToken).ConfigureAwait(false);
        var metadataUnavailable = version.Exists && !trusted;
        if (!trusted)
        {
            prior = new(1, project.ProjectId, [], []);
        }

        var inputs = assetUris.Distinct().Select(uri => CookInputResolver.Resolve(project, uri, ContentCookInputRole.Primary)).ToArray();
        var graph = await new CookDependencyDiscovery(documents, allowUnsavedDocuments: true).DiscoverAsync(project, inputs, cancellationToken).ConfigureAwait(false);
        var availableFiles = graph.Files.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
        var validGraph = graph with
        {
            Assets =
            [
                .. graph.Assets.Where(input => graph.Dependencies.ContainsKey(input.AssetUri)
                    && graph.FileDependencies.TryGetValue(input.AssetUri, out var paths) && paths.All(availableFiles.Contains)),
            ],
        };
        var native = prior.Products.IsEmpty ? null : await nativeCompatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        try
        {
            var plan = await CookIncrementalPlanner.PlanAsync(project, native?.Artifacts?.Fingerprint ?? string.Empty, graph.Files, validGraph, prior, cancellationToken).ConfigureAwait(false);
            var changed = await this.FindChangedInputsAsync(graph.Files, cancellationToken).ConfigureAwait(false);
            using var owners = await documents.AcquireAsync(graph.Assets.Select(static asset => asset.SourceAbsolutePath), cancellationToken).ConfigureAwait(false);
            var products = prior.Products.ToDictionary(static product => product.SourceUri);
            return inputs.Select(input => CreateStatus(
                input,
                graph,
                plan,
                products,
                owners.Documents,
                native?.Succeeded != false,
                native?.Diagnostics ?? [],
                metadataUnavailable,
                changed)).ToArray();
        }
        finally
        {
            if (native?.Artifacts is { } artifacts)
            {
                await artifacts.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static AssetCookStatus CreateStatus(
        ContentCookInput input,
        CookDependencyGraph graph,
        CookIncrementalPlan plan,
        Dictionary<Uri, CookProvenance.Product> products,
        ImmutableArray<CookDocumentState> documents,
        bool nativeAvailable,
        IReadOnlyList<DiagnosticRecord> nativeDiagnostics,
        bool metadataUnavailable,
        HashSet<string> changed)
    {
        var closure = Closure(input.AssetUri, graph.Dependencies);
        var paths = graph.Assets.Where(asset => closure.Contains(asset.AssetUri)).Select(static asset => asset.SourceAbsolutePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var issues = graph.Diagnostics.Where(diagnostic => diagnostic.AffectedPath is { } path && paths.Contains(path)).ToImmutableArray();
        var inputChanged = graph.Assets.Any(asset => closure.Contains(asset.AssetUri)
            && graph.FileDependencies.TryGetValue(asset.AssetUri, out var dependencies) && dependencies.Any(changed.Contains));
        _ = products.TryGetValue(input.AssetUri, out var prior);
        var published = prior?.Outputs.Select(static output => output.Asset).ToImmutableArray() ?? [];
        var verified = prior is not null && VerifyPriorClosure(prior.SourceUri, products, plan.VerifiedOutputs, []);
        var freshness = issues.Any(static issue => string.Equals(issue.Code, AssetImportDiagnosticCodes.SourceMissing, StringComparison.Ordinal)) ? AssetCookFreshness.MissingSource
            : issues.Any(static issue => issue.Severity == DiagnosticSeverity.Error) ? AssetCookFreshness.InvalidSource
            : metadataUnavailable || !nativeAvailable || inputChanged ? AssetCookFreshness.Unknown
            : prior is null ? AssetCookFreshness.NeedsCooking
            : closure.All(plan.Reusable.ContainsKey) && verified ? AssetCookFreshness.Current
            : AssetCookFreshness.OutOfDate;
        if (metadataUnavailable)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.publication_unverified", "Cooked content has no verified publication record. Cook the asset to update it."));
        }

        if (inputChanged)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.input_changed", "Saved inputs changed while checking this asset. Refresh its status."));
        }

        return new(
            input.AssetUri,
            freshness,
            prior is not null,
            verified,
            published,
            [.. documents.Where(document => document.IsDirty && paths.Contains(document.SourcePath))],
            [.. issues, .. nativeDiagnostics]);
    }

    private static DiagnosticRecord StatusIssue(ContentCookInput input, string code, string message) => new()
    {
        OperationId = Guid.Empty,
        Domain = FailureDomain.AssetCook,
        Severity = DiagnosticSeverity.Warning,
        Code = code,
        Message = message,
        AffectedPath = input.SourceAbsolutePath,
        AffectedVirtualPath = input.AssetUri.AbsolutePath,
    };

    private static HashSet<Uri> Closure(Uri source, ImmutableDictionary<Uri, ImmutableArray<Uri>> dependencies)
    {
        var found = new HashSet<Uri>();
        var pending = new Stack<Uri>();
        pending.Push(source);
        while (pending.TryPop(out var current))
        {
            if (found.Add(current) && dependencies.TryGetValue(current, out var children))
            {
                foreach (var child in children)
                {
                    pending.Push(child);
                }
            }
        }

        return found;
    }

    private static bool VerifyPriorClosure(
        Uri source,
        Dictionary<Uri, CookProvenance.Product> products,
        ImmutableHashSet<(string rootMount, string virtualPath)> verified,
        HashSet<Uri> visited)
        => !visited.Add(source) || (products.TryGetValue(source, out var product)
            && product.Outputs.All(output => verified.Contains((output.RootMount, output.Asset.VirtualPath)))
            && product.Dependencies.All(dependency => VerifyPriorClosure(dependency, products, verified, visited)));

    private async Task<HashSet<string>> FindChangedInputsAsync(ImmutableArray<CookSnapshotInput> inputs, CancellationToken cancellationToken)
    {
        var changed = new HashSet<string>(StringComparer.Ordinal);
        foreach (var input in inputs)
        {
            try
            {
                if (input.IsAbsent)
                {
                    if (CookSavedSourceReader.Exists(input.SourcePath))
                    {
                        _ = changed.Add(input.RelativePath);
                    }
                }
                else
                {
                    var bytes = await CookSavedSourceReader.ReadAsync(documents, input.SourcePath, cancellationToken, allowUnsavedDocuments: true).ConfigureAwait(false);
                    if (!string.Equals(Convert.ToHexString(SHA256.HashData(bytes)), input.DiscoveryHash, StringComparison.Ordinal))
                    {
                        _ = changed.Add(input.RelativePath);
                    }
                }
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                _ = changed.Add(input.RelativePath);
            }
        }

        return changed;
    }
}
