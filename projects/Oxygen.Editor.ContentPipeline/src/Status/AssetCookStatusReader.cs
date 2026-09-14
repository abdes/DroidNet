// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Import;
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
public sealed partial class AssetCookStatusReader(
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

        var requested = assetUris.Distinct().ToArray();
        var builtins = requested.Where(IsBuiltinIdentity).ToImmutableArray();
        var imports = await ImportedSourceIndex.ReadAsync(project, documents, prior, cancellationToken).ConfigureAwait(false);
        var mapped = requested.Where(uri => !IsBuiltinIdentity(uri)).Select(uri => (Requested: uri, Resolution: imports.ResolveOutputFacts(project, uri, ContentCookInputRole.Primary))).ToArray();
        var inputs = mapped.Select(item => item.Resolution.Source ?? CookInputResolver.Resolve(project, item.Requested, ContentCookInputRole.Primary)).ToArray();
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, cancellationToken, uri => imports.ResolveOutput(project, uri, ContentCookInputRole.Dependency), imports.KnownOutputs).ConfigureAwait(false);
        var graph = await this.CreateDependencyDiscovery(project, prior, imports, libraries).DiscoverAsync(project, inputs, cancellationToken).ConfigureAwait(false);
        graph = graph with { Builtins = [.. graph.Builtins.Union(builtins)] };
        graph = libraries.Apply(graph);
        var validGraph = WithCompleteAssets(graph);
        var native = prior.Products.IsEmpty ? null : await nativeCompatibility.VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
        try
        {
            var plan = await CookIncrementalPlanner.PlanAsync(project, native?.Artifacts?.Fingerprint ?? string.Empty, graph.Files, validGraph, prior, cancellationToken).ConfigureAwait(false);
            var changed = await this.FindChangedInputsAsync(graph.Files, cancellationToken).ConfigureAwait(false);
            using var owners = await documents.AcquireAsync(graph.Assets.Select(static asset => asset.SourceAbsolutePath), cancellationToken).ConfigureAwait(false);
            var products = prior.Products.ToDictionary(static product => product.SourceUri);
            var sourceStatuses = inputs.DistinctBy(static input => input.AssetUri).ToDictionary(static input => input.AssetUri, input => CreateStatus(
                input,
                graph,
                plan,
                products,
                owners.Documents,
                native?.Succeeded != false,
                native?.Diagnostics ?? [],
                metadataUnavailable,
                changed));
            return inputs.Select((input, index) => MapImportedStatus(mapped[index].Requested, mapped[index].Resolution, sourceStatuses[input.AssetUri]))
                .Concat(builtins.Select(uri => CreateBuiltinStatus(uri, products, plan, native?.Succeeded != false, metadataUnavailable))).ToArray();
        }
        finally
        {
            if (native?.Artifacts is { } artifacts)
            {
                await artifacts.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static CookDependencyGraph WithCompleteAssets(CookDependencyGraph graph)
    {
        var availableFiles = graph.Files.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
        return graph with
        {
            Assets =
            [
                .. graph.Assets.Where(input => !graph.ImportsNeedingDiscovery.Contains(input.AssetUri) && graph.Dependencies.ContainsKey(input.AssetUri)
                    && graph.FileDependencies.TryGetValue(input.AssetUri, out var paths) && paths.All(availableFiles.Contains)),
            ],
        };
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
        var inspectionPending = issues.Any(static issue => string.Equals(issue.Code, "asset_cook.library_inspection_required", StringComparison.Ordinal));
        var libraries = closure.SelectMany(uri => graph.CookedDependencies.GetValueOrDefault(uri, [])).Distinct().ToArray();
        var changedLibrary = prior?.CookedDependencies.Any(dependency => LibraryChanged(dependency, libraries)) == true;
        var published = prior?.Outputs.Select(static output => output.Asset).ToImmutableArray() ?? [];
        var verified = prior is not null && VerifyPriorClosure(prior.SourceUri, products, plan.VerifiedOutputs, graph.CookedDependencies, []);
        var needsDiscovery = closure.Overlaps(graph.ImportsNeedingDiscovery);
        var freshness = issues.Any(static issue => string.Equals(issue.Code, AssetImportDiagnosticCodes.SourceMissing, StringComparison.Ordinal)) ? AssetCookFreshness.MissingSource
            : issues.Any(static issue => issue.Severity == DiagnosticSeverity.Error) ? AssetCookFreshness.InvalidSource
            : metadataUnavailable || !nativeAvailable || inputChanged ? AssetCookFreshness.Unknown
            : inspectionPending ? changedLibrary ? AssetCookFreshness.OutOfDate : AssetCookFreshness.Unknown
            : prior is null ? AssetCookFreshness.NeedsCooking
            : needsDiscovery ? AssetCookFreshness.OutOfDate
            : closure.All(uri => plan.Reusable.ContainsKey(uri) || libraries.Any(dependency => dependency.AssetUri == uri)) && verified ? AssetCookFreshness.Current
            : AssetCookFreshness.OutOfDate;
        if (metadataUnavailable)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.publication_unverified", "Cooked content has no verified publication record. Cook the asset to update it."));
        }

        if (inputChanged)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.input_changed", "Saved inputs changed while checking this asset. Refresh its status."));
        }

        if (needsDiscovery)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.import_source_changed", "The model source changed. Reimport to update its outputs and dependencies."));
        }

        return new(
            input.AssetUri,
            freshness,
            prior is not null,
            verified,
            published,
            [.. documents.Where(document => document.IsDirty && paths.Contains(document.SourcePath))],
            [.. issues, .. nativeDiagnostics])
        {
            SourcePaths = [.. paths],
            SavedSourceHash = graph.Files.FirstOrDefault(file => string.Equals(file.SourcePath, input.SourceAbsolutePath, StringComparison.OrdinalIgnoreCase))?.DiscoveryHash,
        };
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

    private static AssetCookStatus MapImportedStatus(Uri requested, ImportedSourceIndex.Resolution resolution, AssetCookStatus status)
    {
        if (resolution.Error is { } error)
        {
            return status with
            {
                AssetUri = requested,
                Freshness = AssetCookFreshness.InvalidSource,
                HasVerifiedOutput = false,
                Diagnostics = [new DiagnosticRecord { OperationId = Guid.Empty, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error, Code = "asset_status.import_conflict", Message = error, AffectedVirtualPath = requested.AbsolutePath }],
            };
        }

        if (resolution.Source is null)
        {
            return status;
        }

        var available = status.Outputs.Any(output => output.CookedAssetUri == requested);
        return status with
        {
            AssetUri = requested,
            HasPublishedOutput = available,
            HasVerifiedOutput = available && status.HasVerifiedOutput,
            Freshness = !available && status.Freshness == AssetCookFreshness.Current ? AssetCookFreshness.NeedsCooking : status.Freshness,
        };
    }

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

    private static bool LibraryChanged(CookedDependencySnapshot prior, CookedDependencySnapshot[] libraries)
    {
        var current = libraries.FirstOrDefault(item => string.Equals(item.AssetKey, prior.AssetKey, StringComparison.OrdinalIgnoreCase))
            ?? libraries.FirstOrDefault(item => item.AssetUri == prior.AssetUri);
        return current is not null && current != prior;
    }

    private static bool VerifyPriorClosure(
        Uri source,
        Dictionary<Uri, CookProvenance.Product> products,
        ImmutableHashSet<(string rootMount, string virtualPath)> verified,
        ImmutableDictionary<Uri, ImmutableArray<CookedDependencySnapshot>> cookedDependencies,
        HashSet<Uri> visited)
        => !visited.Add(source) || (products.TryGetValue(source, out var product)
            && product.Outputs.All(output => verified.Contains((output.RootMount, output.Asset.VirtualPath)))
            && product.CookedDependencies.All(dependency => cookedDependencies.GetValueOrDefault(source, []).Contains(dependency))
            && product.Dependencies.All(dependency => product.CookedDependencies.Any(input => input.AssetUri == dependency)
                || VerifyPriorClosure(dependency, products, verified, cookedDependencies, visited)));

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
                    var hash = await CookSavedSourceReader.HashAsync(documents, input.SourcePath, cancellationToken, allowUnsavedDocuments: true).ConfigureAwait(false);
                    if (!string.Equals(hash, input.DiscoveryHash, StringComparison.Ordinal))
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

    private CookDependencyDiscovery CreateDependencyDiscovery(ProjectContext project, CookProvenance prior, ImportedSourceIndex imports, CookedLibraryReadSet libraries)
        => new(
            documents,
            allowUnsavedDocuments: true,
            importedSources: prior.Products.Where(static product => product.ImportedSource is not null).ToDictionary(static product => product.SourceUri, static product => product.ImportedSource!),
            resolveImported: uri => imports.ResolveOutput(project, uri, ContentCookInputRole.Dependency),
            preferCookedReference: libraries.IsLibraryPreferred,
            expandCookedReferences: (input, references, token) => libraries.ExpandReferencesAsync(input, references, inspector: null, operationRoot: string.Empty, artifacts: null, token));
}
