// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Reads the same input fingerprints and output evidence used by incremental cooking.</summary>
/// <remarks>
/// A status describes the source files on disk against the published content. It reads the files directly; open
/// documents and their unsaved changes are not part of it.
/// </remarks>
/// <param name="publication">The committed metadata verifier.</param>
/// <param name="engineContent">The engine content pak, the lowest library engine references resolve through.</param>
/// <param name="loggerFactory">Creates the logger of the read times.</param>
public sealed partial class AssetCookStatusReader(
    CookPublicationService publication,
    IEngineContentPak engineContent,
    ILoggerFactory? loggerFactory = null) : IAssetCookStatusReader
{
    /// <summary>The code of the status of an asset that cannot be a cook input, such as a deleted script.</summary>
    internal const string InputUnresolvedCode = "asset_status.input_unresolved";

    private readonly ILogger logger = loggerFactory?.CreateLogger<AssetCookStatusReader>() ?? NullLogger<AssetCookStatusReader>.Instance;

    /// <inheritdoc />
    public async Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(assetUris);
        cancellationToken.ThrowIfCancellationRequested();
        if (assetUris.Count == 0)
        {
            return [];
        }

        using var output = await publication.AcquireReadAsync(project, cancellationToken).ConfigureAwait(false);
        return await this.ReadAsync(project, output, assetUris, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    Task<IReadOnlyList<AssetCookStatus>> IAssetCookStatusReader.ReadAsync(ProjectContext project, ICookPublicationReadLease publication, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken)
        => this.ReadAsync(project, CookPublicationReadLease.Issued(publication), assetUris, cancellationToken);

    /// <summary>Reads status while the caller retains the project's finite inspection lease.</summary>
    /// <param name="project">The protected project.</param>
    /// <param name="snapshot">The retained publication selection.</param>
    /// <param name="assetUris">The requested authored identities.</param>
    /// <param name="cancellationToken">Cancels status reads.</param>
    /// <returns>The current source and output facts.</returns>
    public async Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, CookPublicationReadLease snapshot, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
    {
        using var retained = snapshot.Retain();
        if (snapshot.ProjectId != project.ProjectId || snapshot.ProjectRoot != project.ProjectRoot)
        {
            throw new ArgumentException("The status publication belongs to another project.", nameof(snapshot));
        }

        var prior = snapshot.ProductState;
        const bool metadataUnavailable = false;

        var started = Stopwatch.GetTimestamp();
        var requested = assetUris.Distinct().ToArray();
        var resolved = requested
            .Select(uri => (Requested: uri, Container: CookInputResolver.ResolveContainer(project, uri, ContentCookInputRole.Primary)))
            .Select(item => (item.Requested, item.Container, Input: ResolveInput(project, item.Requested, item.Container)))
            .ToArray();

        // An asset that cannot be a cook input, such as one outside the project, is invalid on its own; the others
        // are read as usual.
        var unresolved = resolved.Select(static item => item.Input.Unresolved).OfType<AssetCookStatus>().ToArray();
        var mapped = resolved.Where(static item => item.Input.Input is not null).Select(static item => (item.Requested, item.Container)).ToArray();
        var inputs = resolved.Select(static item => item.Input.Input).OfType<ContentCookInput>().ToArray();
        var inputsResolved = Stopwatch.GetTimestamp();
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, engineContent, cancellationToken, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. prior.ProducedAssets]).ConfigureAwait(false);
        var librariesRead = Stopwatch.GetTimestamp();
        var graph = await CreateDependencyDiscovery(project, prior, libraries).DiscoverAsync(project, inputs, cancellationToken).ConfigureAwait(false);
        graph = libraries.Apply(graph);
        var validGraph = WithCompleteAssets(graph);
        var discovered = Stopwatch.GetTimestamp();
        var plan = await ReadFreshnessAsync(snapshot, validGraph, prior, cancellationToken).ConfigureAwait(false);
        var freshnessRead = Stopwatch.GetTimestamp();
        var changed = await FindChangedInputsAsync(graph.Files, cancellationToken).ConfigureAwait(false);
        this.LogReadTimes(new(started, inputsResolved, librariesRead, discovered, freshnessRead, Stopwatch.GetTimestamp()), requested.Length, graph.Files.Length);
        var products = prior.Products.ToDictionary(static product => product.SourceUri);
        var sourceStatuses = inputs.DistinctBy(static input => input.AssetUri).ToDictionary(static input => input.AssetUri, input => CreateStatus(
            input,
            graph,
            plan,
            products,
            metadataUnavailable,
            changed));
        return [
            .. inputs.Select((input, index) => MapProductStatus(mapped[index].Requested, mapped[index].Container, sourceStatuses[input.AssetUri]))
                .Concat(unresolved)
                .Select(state => state with { OutputRoots = snapshot.ProjectRoots }),
        ];
    }

    // The cook input of a requested asset, or, when it cannot be one, the asset's invalid status.
    private static (ContentCookInput? Input, AssetCookStatus? Unresolved) ResolveInput(ProjectContext project, AssetPath requested, ContentCookInput? container)
    {
        if (container is not null)
        {
            return (container, null);
        }

        try
        {
            return (CookInputResolver.Resolve(project, requested, ContentCookInputRole.Primary), null);
        }
        catch (InvalidDataException failure)
        {
            return (null, CreateUnresolvedStatus(requested, failure.Message));
        }
    }

    private static AssetCookStatus CreateUnresolvedStatus(AssetPath requested, string reason)
        => new(
            requested,
            AssetCookFreshness.InvalidSource,
            HasPublishedOutput: false,
            CookedOutputAvailability.Unknown,
            [],
            [
                new DiagnosticRecord
                {
                    OperationId = Guid.Empty,
                    Domain = FailureDomain.AssetCook,
                    Severity = DiagnosticSeverity.Error,
                    Code = InputUnresolvedCode,
                    Message = reason,
                    AffectedVirtualPath = requested.VirtualPath,
                },
            ]);

    private static CookDependencyGraph WithCompleteAssets(CookDependencyGraph graph)
    {
        var availableFiles = graph.Files.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
        return graph with
        {
            Assets =
            [
                .. graph.Assets.Where(input => !graph.SourcesNeedingAnalysis.Contains(input.AssetUri) && graph.Dependencies.ContainsKey(input.AssetUri)
                    && graph.FileDependencies.TryGetValue(input.AssetUri, out var paths) && paths.All(availableFiles.Contains)),
            ],
        };
    }

    private static AssetCookStatus CreateStatus(
        ContentCookInput input,
        CookDependencyGraph graph,
        FreshnessSnapshot plan,
        Dictionary<AssetPath, CookProvenance.Product> products,
        bool metadataUnavailable,
        HashSet<string> changed)
    {
        var closure = Closure(input.AssetUri, graph.Dependencies);
        var paths = graph.Assets.Where(asset => closure.Contains(asset.AssetUri)).Select(static asset => asset.SourceAbsolutePath).ToHashSet();
        var issues = graph.Diagnostics.Where(diagnostic => diagnostic.AffectedPath is { } path && paths.Contains(FileSystemPath.From(path))).ToImmutableArray();
        var inputChanged = graph.Assets.Any(asset => closure.Contains(asset.AssetUri)
            && graph.FileDependencies.TryGetValue(asset.AssetUri, out var dependencies) && dependencies.Any(changed.Contains));
        _ = products.TryGetValue(input.AssetUri, out var prior);
        var inspectionPending = issues.Any(static issue => string.Equals(issue.Code, "asset_cook.library_inspection_required", StringComparison.Ordinal));
        var libraries = closure.SelectMany(uri => graph.CookedDependencies.GetValueOrDefault(uri, [])).Distinct().ToArray();
        var changedLibrary = prior?.CookedDependencies.Any(dependency => LibraryChanged(dependency, libraries)) == true;
        var published = prior?.Outputs.Select(static output => output.Asset).ToImmutableArray() ?? [];
        var availability = metadataUnavailable ? CookedOutputAvailability.Unknown
            : prior is null ? CookedOutputAvailability.Missing : ReadOutputAvailability(prior.SourceUri, products, plan, []);
        var needsDiscovery = closure.Overlaps(graph.SourcesNeedingAnalysis);
        var freshness = issues.Any(static issue => string.Equals(issue.Code, AssetImportDiagnosticCodes.SourceMissing, StringComparison.Ordinal)) ? AssetCookFreshness.MissingSource
            : issues.Any(static issue => issue.Severity == DiagnosticSeverity.Error) ? AssetCookFreshness.InvalidSource
            : metadataUnavailable || inputChanged ? AssetCookFreshness.Unknown
            : inspectionPending ? changedLibrary ? AssetCookFreshness.OutOfDate : AssetCookFreshness.Unknown
            : prior is null ? AssetCookFreshness.NeedsCooking
            : prior.SourceInput is null || prior.SourceFiles.IsEmpty ? AssetCookFreshness.Unknown
            : needsDiscovery ? AssetCookFreshness.OutOfDate
            : closure.All(uri => plan.CurrentProducts.Contains(uri) || libraries.Any(dependency => dependency.AssetUri == uri)) && availability == CookedOutputAvailability.Present ? AssetCookFreshness.Current
            : AssetCookFreshness.OutOfDate;
        if (metadataUnavailable)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.publication_unverified", "Cooked content has no verified publication record. Cook the asset to update it."));
        }

        if (inputChanged)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.input_changed", "Saved inputs changed while checking this asset. Refresh its status."));
        }

        if (needsDiscovery && prior is not null && input.Kind == ContentCookAssetKind.ForeignSource)
        {
            issues = issues.Add(StatusIssue(input, "asset_status.import_source_changed", "The model source changed. Reimport to update its outputs and dependencies."));
        }

        return new(
            input.AssetUri,
            freshness,
            prior is not null,
            availability,
            published,
            issues)
        {
            SourcePaths = [.. paths],
            SavedSourceHash = graph.Files.FirstOrDefault(file => file.SourcePath == input.SourceAbsolutePath)?.DiscoveryHash,
        };
    }

    private static DiagnosticRecord StatusIssue(ContentCookInput input, string code, string message) => new()
    {
        OperationId = Guid.Empty,
        Domain = FailureDomain.AssetCook,
        Severity = DiagnosticSeverity.Warning,
        Code = code,
        Message = message,
        AffectedPath = input.SourceAbsolutePath.FullPath,
        AffectedVirtualPath = input.AssetUri.VirtualPath,
    };

    // A container's product reads its container's status, and is available when the container published it.
    private static AssetCookStatus MapProductStatus(AssetPath requested, ContentCookInput? container, AssetCookStatus status)
    {
        if (container is null)
        {
            return status with { AssetUri = requested };
        }

        var available = status.Outputs.Any(output => output.CookedAssetUri == requested);
        return status with
        {
            AssetUri = requested,
            HasPublishedOutput = available,
            OutputAvailability = available ? status.OutputAvailability : CookedOutputAvailability.Missing,
            Freshness = !available && status.Freshness == AssetCookFreshness.Current ? AssetCookFreshness.NeedsCooking : status.Freshness,
        };
    }

    private static HashSet<AssetPath> Closure(AssetPath source, ImmutableDictionary<AssetPath, ImmutableArray<AssetPath>> dependencies)
    {
        var found = new HashSet<AssetPath>();
        var pending = new Stack<AssetPath>();
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

    private static async Task<HashSet<string>> FindChangedInputsAsync(ImmutableArray<CookSnapshotInput> inputs, CancellationToken cancellationToken)
    {
        var changed = new HashSet<string>(StringComparer.Ordinal);
        foreach (var input in inputs)
        {
            try
            {
                if (input.Kind != CookSnapshotInputKind.File)
                {
                    if (!CookSavedSourceReader.MatchesProbe(input))
                    {
                        _ = changed.Add(input.RelativePath);
                    }
                }
                else
                {
                    var hash = await CookSavedSourceReader.HashFileAsync(input.SourcePath.FullPath, cancellationToken).ConfigureAwait(false);
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

    private static CookDependencyDiscovery CreateDependencyDiscovery(ProjectContext project, CookProvenance prior, CookedLibraryReadSet libraries)
        => new(
            new AcceptedCookSourceFacts(project, prior, CookSavedSourceReader.HashFileAsync),
            resolveImported: uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency),
            resolveProducer: prior.CreateProducerLookup(project),
            preferCookedReference: libraries.IsLibraryPreferred,
            expandCookedReferences: (input, references, token) => libraries.ExpandReferencesAsync(input, references, inspector: null, operationRoot: string.Empty, artifacts: null, token));
}
