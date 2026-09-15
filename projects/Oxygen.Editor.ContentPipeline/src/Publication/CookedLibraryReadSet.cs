// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Captures immutable cooked-library facts and retains the corresponding file readers.</summary>
internal sealed partial class CookedLibraryReadSet : IDisposable
{
    private readonly List<Root> roots = [];
    private readonly List<(string name, string message)> failures = [];
    private IReadOnlyList<CookedContentSource> order = [];
    private string projectRoot = string.Empty;
    private ProjectContext project = null!;
    private Func<Uri, ContentCookInput?>? resolveImported;
    private IReadOnlyCollection<Uri> knownProjectOutputs = [];
    private ProjectAssetKeyIndex? projectIdentities;

    /// <summary>Gets a value indicating whether cached dependencies name keys absent from every declared library index.</summary>
    public bool HasUnresolvedAssetKeys => this.roots.Where(static root => root.Dependencies is not null)
        .SelectMany(static root => root.Dependencies!.Assets.Values).SelectMany(static asset => asset.Dependencies)
        .Any(key => this.FindKey(key).asset is null);

    /// <summary>Reads library indexes and protects the full container inputs without starting a native worker.</summary>
    /// <param name="project">The project declaring the ordered libraries.</param>
    /// <param name="cancellationToken">Cancels acquisition and hashing.</param>
    /// <param name="resolveImported">Resolves retained project owners of imported outputs.</param>
    /// <param name="knownOutputs">Previously identified imported project output names.</param>
    /// <returns>The selected library readers and lookup facts.</returns>
    public static async Task<CookedLibraryReadSet> AcquireAsync(ProjectContext project, CancellationToken cancellationToken, Func<Uri, ContentCookInput?>? resolveImported = null, IReadOnlyCollection<Uri>? knownOutputs = null)
    {
        var result = new CookedLibraryReadSet { order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder), projectRoot = project.ProjectRoot, project = project, resolveImported = resolveImported, knownProjectOutputs = knownOutputs ?? [] };
        try
        {
            foreach (var source in result.order.Where(static source => source.Kind == CookedContentSourceKind.LocalFolder))
            {
                var mount = project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.OrdinalIgnoreCase));
                var path = Path.GetFullPath(mount.AbsolutePath);
                if (!File.Exists(Path.Combine(path, "container.index.bin")))
                {
                    continue;
                }

                CookOutputReadLease? files = null;
                try
                {
                    CookOutputLease.RejectReparsePoint(path);
                    files = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
                    using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(new RealFileSystem()), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = path });
                    var records = await catalog.QueryAsync(new(AssetQueryScope.All), cancellationToken).ConfigureAwait(false);
                    await files.VerifyDescriptorsAsync(records, cancellationToken).ConfigureAwait(false);
                    var fingerprint = await CookedDependencyCache.FingerprintAsync(files, cancellationToken).ConfigureAwait(false);
                    var metadata = await CookedDependencyCache.ReadAsync(project.ProjectRoot, fingerprint, records, cancellationToken).ConfigureAwait(false);
                    result.roots.Add(new(mount.Name, path, records, fingerprint, files) { Dependencies = metadata });
                    files = null;
                }
                catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
                {
                    result.failures.Add((mount.Name, failure.Message));
                }
                finally
                {
                    if (files is not null)
                    {
                        await files.DisposeAsync().ConfigureAwait(false);
                    }
                }
            }

            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    /// <summary>Includes captured library identities in the publication input record.</summary>
    /// <param name="snapshot">The coherently captured saved inputs.</param>
    /// <param name="graph">The closure with selected library dependencies.</param>
    /// <returns>The complete publication input identity.</returns>
    public static CookInputSnapshot CaptureDependencies(CookInputSnapshot snapshot, CookDependencyGraph graph)
    {
        var dependencies = graph.CookedDependencies.Values.SelectMany(static dependencies => dependencies).Distinct().OrderBy(static dependency => dependency.AssetUri.AbsoluteUri, StringComparer.Ordinal).ThenBy(static dependency => dependency.AssetKey, StringComparer.Ordinal).ToImmutableArray();
        return dependencies.IsEmpty ? snapshot : snapshot with
        {
            CookedDependencies = dependencies,
            InputIdentity = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new { snapshot.InputIdentity, Libraries = dependencies }))),
        };
    }

    /// <summary>Tests whether the saved order resolves this identity to a library instead of a project source.</summary>
    /// <param name="uri">The authored or native identity.</param>
    /// <returns>Whether dependency discovery should retain the cooked identity.</returns>
    public bool IsLibraryPreferred(Uri uri)
    {
        var nativeUri = uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? new Uri(uri.AbsoluteUri[..^5]) : uri;
        var (root, asset) = this.FindUri(nativeUri);
        return asset is not null && !this.ProjectSourceWins(nativeUri, root);
    }

    /// <summary>Resolves embedded keys separately from authored virtual-path references.</summary>
    /// <param name="consumer">The source whose dependency closure is being captured.</param>
    /// <param name="references">Direct authored references read from this source.</param>
    /// <param name="inspector">The owned cook's native decoder, or null for cached status reads.</param>
    /// <param name="operationRoot">Private native scratch space.</param>
    /// <param name="artifacts">Borrowed native artifact ownership.</param>
    /// <param name="cancellationToken">Cancels lookup and inspection.</param>
    /// <returns>Exact project and library identities required by this consumer.</returns>
    public async Task<CookReferenceExpansion> ExpandReferencesAsync(ContentCookInput consumer, IReadOnlyList<Uri> references, ICookedDependencyInspector? inspector, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        var projects = new Dictionary<Uri, ContentCookInput>();
        var importedOutputs = new HashSet<Uri>();
        var libraries = new List<CookedDependencySnapshot>();
        var diagnostics = new List<DiagnosticRecord>();
        var pending = this.CreateReferenceQueue(references);

        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (pending.TryDequeue(out var current))
        {
            var (root, asset) = current;
            var key = asset.Cooked!.AssetKey.ToString();
            if (!visited.Add(key))
            {
                continue;
            }

            libraries.Add(new(asset.Uri, root.Name, root.Path, key, asset.Cooked.AssetType, root.Fingerprint));
            var metadata = await this.GetDependencyMetadataAsync(root, key, inspector, operationRoot, artifacts, cancellationToken).ConfigureAwait(false);
            if (metadata?.Complete != true)
            {
                diagnostics.Add(metadata is null
                    ? Issue(consumer, "asset_cook.library_inspection_required", "Library dependencies are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning }
                    : Issue(consumer, "asset_cook.library_inspection_failed", metadata.Diagnostic ?? "Library dependencies could not be inspected."));
                continue;
            }

            foreach (var dependency in metadata.Dependencies)
            {
                var found = this.FindKey(dependency);
                var candidate = found.asset is null || this.ProjectSourceWins(found.asset.Uri, found.root)
                    ? await this.ResolveProjectKeyAsync(dependency, inspector as ICookedAssetKeyProvider, operationRoot, artifacts, cancellationToken).ConfigureAwait(false) : null;
                if (candidate is not null && (found.asset is null || this.ProjectHasPriority(found.root)))
                {
                    var source = this.resolveImported?.Invoke(candidate) ?? CookInputResolver.Resolve(this.project, candidate, ContentCookInputRole.Dependency);
                    projects[source.AssetUri] = source;
                    if (source.Kind == ContentCookAssetKind.ForeignSource)
                    {
                        _ = importedOutputs.Add(candidate);
                    }
                }
                else if (found.asset is not null)
                {
                    if (this.projectIdentities?.IsPending == true && this.ProjectSourceWins(found.asset.Uri, found.root))
                    {
                        diagnostics.Add(Issue(consumer, "asset_cook.library_inspection_required", "Project asset identities are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning });
                    }

                    pending.Enqueue(found);
                }
                else
                {
                    diagnostics.Add(this.projectIdentities?.IsPending == true
                        ? Issue(consumer, "asset_cook.library_inspection_required", "Project asset identities are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning }
                        : Issue(consumer, "asset_cook.library_dependency_missing", $"Library asset '{asset.Uri}' requires missing asset '{dependency}'."));
                }
            }
        }

        return new([.. projects.Values], [.. libraries], [.. diagnostics]) { ImportedOutputs = [.. importedOutputs] };
    }

    /// <summary>Attaches selected library inputs and reports required references that remain unavailable.</summary>
    /// <param name="graph">The discovered authoring closure.</param>
    /// <returns>The closure with selected library dependencies and scoped issues.</returns>
    public CookDependencyGraph Apply(CookDependencyGraph graph)
    {
        var diagnostics = graph.Diagnostics.ToBuilder();
        foreach (var (name, message) in this.failures)
        {
            foreach (var consumer in graph.Assets.Where(input => graph.NativeReferences.GetValueOrDefault(input.AssetUri, []).Length != 0))
            {
                diagnostics.Add(Issue(consumer, "asset_cook.library_unavailable", $"Cannot read cooked library '{name}': {message}"));
            }
        }

        var requested = graph.NativeReferences.Values.SelectMany(static values => values).Concat(graph.PublishedReferences).Distinct();
        var priority = this.order.Select((source, index) => (source, index)).ToArray();
        var projectPriority = priority.Single(static item => item.source.Kind == CookedContentSourceKind.ProjectOutput).index;
        foreach (var uri in requested)
        {
            var match = this.roots.AsEnumerable().Reverse().Select(root => (root, record: root.Assets.FirstOrDefault(record => record.Uri == uri))).FirstOrDefault(static item => item.record is not null);
            var owned = HasProjectOwner(graph, uri);
            if (match.record?.Cooked is { } asset)
            {
                var libraryPriority = priority.Single(item => string.Equals(item.source.Name, match.root.Name, StringComparison.OrdinalIgnoreCase)).index;
                if (owned && projectPriority > libraryPriority)
                {
                    continue;
                }

                var expected = Path.GetExtension(uri.AbsolutePath).ToUpperInvariant() switch { ".OMAT" => 1, ".OGEO" => 2, ".OSCENE" => 3, _ => 0 };
                if (expected != 0 && asset.AssetType != expected)
                {
                    AddIssue(uri, "asset_cook.library_type_mismatch", "The selected library asset has the wrong type.");
                    continue;
                }
            }
            else if (graph.PublishedReferences.Contains(uri) && !owned)
            {
                AddIssue(uri, "asset_cook.library_reference_missing", $"Cooked reference '{uri}' was not found in the declared libraries.");
            }
        }

        var usedRoots = graph.CookedDependencies.Values.SelectMany(static bindings => bindings).Select(static binding => binding.RootPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var unused in this.roots.Where(root => !usedRoots.Contains(root.Path)).ToArray())
        {
            unused.Reader.Dispose();
            _ = this.roots.Remove(unused);
        }

        return graph with { Diagnostics = diagnostics.ToImmutable() };

        void AddIssue(Uri uri, string code, string message)
        {
            foreach (var consumer in graph.NativeReferences.Where(pair => pair.Value.Contains(uri)).Select(pair => graph.Assets.Single(input => input.AssetUri == pair.Key)))
            {
                diagnostics.Add(Issue(consumer, code, message));
            }
        }
    }

    /// <summary>Uses the same saved low-to-high source order as runtime mounting.</summary>
    /// <param name="projectRoots">The operation's staged and reusable project roots.</param>
    /// <returns>Native lookup roots ordered from lowest to highest priority.</returns>
    public IReadOnlyList<string> OrderRoots(IEnumerable<string> projectRoots)
    {
        var paths = this.order.SelectMany(source => source.Kind == CookedContentSourceKind.ProjectOutput ? projectRoots.Order(StringComparer.Ordinal)
            : this.roots.Where(root => string.Equals(root.Name, source.Name, StringComparison.OrdinalIgnoreCase)).Select(static root => root.Path));
        return paths.Reverse().Distinct(StringComparer.OrdinalIgnoreCase).Reverse().ToArray();
    }

    /// <summary>Validates native container structure before consumers can use library data.</summary>
    /// <param name="native">The native container validator.</param>
    /// <param name="cancellationToken">Cancels native validation.</param>
    /// <returns>The asynchronous validation.</returns>
    public async Task ValidateNativeAsync(IEngineContentPipelineApi native, CancellationToken cancellationToken)
    {
        foreach (var root in this.roots)
        {
            var result = await native.ValidateLooseCookedRootAsync(root.Path, cancellationToken).ConfigureAwait(false);
            if (!result.Succeeded)
            {
                throw new CookInputDiscoveryException(result.Diagnostics);
            }
        }

        this.Verify();
    }

    /// <summary>Checks that no root gained or lost files while its existing bytes remained protected.</summary>
    public void Verify()
    {
        foreach (var root in this.roots)
        {
            _ = root.Reader.GetFiles();
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var root in this.roots)
        {
            root.Reader.Dispose();
        }
    }

    private static bool HasProjectOwner(CookDependencyGraph graph, Uri uri)
        => graph.Assets.Any(input => input.OwnsOutput(uri.AbsolutePath));

    private static DiagnosticRecord Issue(ContentCookInput consumer, string code, string message)
        => new() { OperationId = Guid.Empty, Domain = FailureDomain.AssetCook, Severity = DiagnosticSeverity.Error, Code = code, Message = message, AffectedPath = consumer.SourceAbsolutePath, AffectedVirtualPath = consumer.AssetUri.AbsolutePath };

    private Queue<(Root root, AssetRecord asset)> CreateReferenceQueue(IReadOnlyList<Uri> references)
    {
        var pending = new Queue<(Root root, AssetRecord asset)>();
        foreach (var uri in references)
        {
            var nativeUri = uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? new Uri(uri.AbsoluteUri[..^5]) : uri;
            var (root, asset) = this.FindUri(nativeUri);
            if (asset is not null && !this.ProjectSourceWins(nativeUri, root))
            {
                pending.Enqueue(this.FindKey(asset.Cooked!.AssetKey.ToString()));
            }
        }

        return pending;
    }

    private async Task<CookedAssetDependencies?> GetDependencyMetadataAsync(Root root, string key, ICookedDependencyInspector? inspector, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        if (root.Dependencies is null && inspector is not null)
        {
            root.Dependencies = await CookedDependencyCache.EnsureAsync(this.projectRoot, root.Path, root.Fingerprint, root.Assets, inspector, operationRoot, cancellationToken, artifacts).ConfigureAwait(false);
        }

        return root.Dependencies?.Assets[key];
    }

    private async Task<Uri?> ResolveProjectKeyAsync(string key, ICookedAssetKeyProvider? provider, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        this.projectIdentities ??= await ProjectAssetKeyIndex.ReadAsync(this.project, this.knownProjectOutputs, cancellationToken).ConfigureAwait(false);
        if (provider is not null)
        {
            _ = await this.projectIdentities.EnsureAsync(provider, operationRoot, cancellationToken, artifacts).ConfigureAwait(false);
        }

        return this.projectIdentities.Resolve(key);
    }

    private bool ProjectHasPriority(Root root)
        => this.order.Last(source => source.Kind == CookedContentSourceKind.ProjectOutput
            || (source.Kind == CookedContentSourceKind.LocalFolder && string.Equals(source.Name, root.Name, StringComparison.OrdinalIgnoreCase))).Kind == CookedContentSourceKind.ProjectOutput;

    private bool ProjectSourceWins(Uri uri, Root root)
        => this.ProjectHasPriority(root) && (this.resolveImported?.Invoke(uri) is not null
            || (CookInputResolver.IsAuthoringUri(this.project, uri)
                && File.Exists(CookInputResolver.Resolve(this.project, uri, ContentCookInputRole.Dependency).SourceAbsolutePath)));

    private (Root root, AssetRecord asset) FindUri(Uri uri)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => asset.Uri == uri).Select(asset => (root, asset))).FirstOrDefault();

    private (Root root, AssetRecord asset) FindKey(string key)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => string.Equals(asset.Cooked?.AssetKey.ToString(), key, StringComparison.OrdinalIgnoreCase)).Select(asset => (root, asset))).FirstOrDefault();

    private sealed record Root(string Name, string Path, IReadOnlyList<AssetRecord> Assets, string Fingerprint, CookOutputReadLease Reader)
    {
        public CookedDependencyReport? Dependencies { get; set; }
    }
}
