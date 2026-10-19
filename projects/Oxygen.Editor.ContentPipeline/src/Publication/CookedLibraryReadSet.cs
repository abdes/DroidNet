// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Oxygen.Managed.Assets.Catalog.Pak;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Captures immutable cooked-library facts and retains the corresponding file readers.</summary>
internal sealed partial class CookedLibraryReadSet : IDisposable
{
    private readonly List<Root> roots = [];
    private readonly List<UnreadableLibrary> unreadable = [];
    private IReadOnlyList<CookedContentSource> order = [];
    private string projectRoot = string.Empty;
    private ProjectContext project = null!;
    private Func<AssetPath, ContentCookInput?>? resolveImported;
    private IReadOnlyCollection<AssetPath> knownProjectOutputs = [];
    private ProjectAssetKeyIndex? projectIdentities;

    /// <summary>Gets the declared libraries that could not be read; they are left out of every binding until they can be.</summary>
    public IReadOnlyList<UnreadableLibrary> Unreadable => this.unreadable;

    /// <summary>Reads library indexes and protects the full container inputs without starting a native worker.</summary>
    /// <param name="project">The project declaring the ordered libraries.</param>
    /// <param name="engineContent">The engine content pak, the lowest library beneath the declared ones.</param>
    /// <param name="cancellationToken">Cancels acquisition and hashing.</param>
    /// <param name="resolveImported">Resolves retained project owners of imported outputs.</param>
    /// <param name="knownOutputs">The assets the project's publication produced, from its provenance.</param>
    /// <returns>The selected library readers and lookup facts.</returns>
    public static async Task<CookedLibraryReadSet> AcquireAsync(ProjectContext project, IEngineContentPak engineContent, CancellationToken cancellationToken, Func<AssetPath, ContentCookInput?>? resolveImported = null, IReadOnlyCollection<AssetPath>? knownOutputs = null)
    {
        ArgumentNullException.ThrowIfNull(engineContent);
        var result = new CookedLibraryReadSet { order = CookedContentOrdering.Resolve(project), projectRoot = project.ProjectRoot.FullPath, project = project, resolveImported = resolveImported, knownProjectOutputs = knownOutputs ?? [] };
        try
        {
            await result.AddEngineContentAsync(engineContent, cancellationToken).ConfigureAwait(false);
            foreach (var source in result.order)
            {
                switch (source.Kind)
                {
                    case CookedContentSourceKind.LocalFolder:
                        await result.AddLooseFolderAsync(project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal)), cancellationToken).ConfigureAwait(false);
                        break;
                    case CookedContentSourceKind.PakFile:
                        await result.AddPakFileAsync(project.PakFileMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.Ordinal)), cancellationToken).ConfigureAwait(false);
                        break;
                    case CookedContentSourceKind.ProjectOutput:
                        break;
                    default:
                        throw new InvalidDataException("Content priority names an unknown source kind.");
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
        var dependencies = graph.CookedDependencies.Values.SelectMany(static dependencies => dependencies).Distinct().OrderBy(static dependency => dependency.AssetUri).ThenBy(static dependency => dependency.AssetKey, StringComparer.Ordinal).ToImmutableArray();
        return dependencies.IsEmpty ? snapshot : snapshot with
        {
            CookedDependencies = dependencies,
            InputIdentity = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new { snapshot.InputIdentity, Libraries = dependencies }))),
        };
    }

    /// <summary>Tests whether the saved order resolves this identity to a library instead of a project source.</summary>
    /// <param name="uri">The asset's identity.</param>
    /// <returns>Whether dependency discovery should retain the cooked identity.</returns>
    public bool IsLibraryPreferred(AssetPath uri)
    {
        var (root, asset) = this.FindUri(uri);
        return asset is not null && !this.ProjectSourceWins(uri, root);
    }

    /// <summary>Finds the selected library asset while this read set retains its files.</summary>
    /// <param name="uri">The asset's identity.</param>
    /// <returns>The preferred physical root and index entry, or null when the project wins.</returns>
    public PreferredLibraryAsset? FindPreferredAsset(AssetPath uri)
    {
        var (root, asset) = this.FindUri(uri);
        if (asset is null || this.ProjectSourceWins(uri, root))
        {
            return null;
        }

        var selected = this.FindKey(asset.Cooked!.AssetKey.ToString());
        return new(selected.root.Path, selected.asset);
    }

    /// <summary>Resolves embedded keys separately from authored virtual-path references.</summary>
    /// <param name="consumer">The source whose dependency closure is being captured.</param>
    /// <param name="references">Direct authored references read from this source.</param>
    /// <param name="inspector">The owned cook's native decoder, or null for cached status reads.</param>
    /// <param name="operationRoot">Private native scratch space.</param>
    /// <param name="artifacts">Borrowed native artifact ownership.</param>
    /// <param name="cancellationToken">Cancels lookup and inspection.</param>
    /// <returns>Exact project and library identities required by this consumer.</returns>
    public async Task<CookReferenceExpansion> ExpandReferencesAsync(ContentCookInput consumer, IReadOnlyList<AssetPath> references, ICookedDependencyInspector? inspector, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        var projects = new Dictionary<AssetPath, ContentCookInput>();
        var importedOutputs = new HashSet<AssetPath>();
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

            libraries.Add(new(asset.Identity, root.Name, root.Path, key, asset.Cooked.AssetType, root.Fingerprint));
            if (asset.Cooked.AssetType == (byte)AssetType.Unknown)
            {
                // A cooked texture is a resource and references nothing.
                continue;
            }

            var metadata = await this.GetDependencyMetadataAsync(root, key, inspector, operationRoot, artifacts, cancellationToken).ConfigureAwait(false);
            if (metadata?.Complete != true)
            {
                diagnostics.Add(IncompleteInspectionIssue(consumer, metadata));
                continue;
            }

            foreach (var reference in metadata.KeyReferences.Where(static reference => reference.TargetKind is CookedKeyReferenceTargetKind.Asset or CookedKeyReferenceTargetKind.Texture))
            {
                var dependency = reference.AssetKey;
                var found = this.FindKey(dependency);
                if (found.asset?.Cooked is { } target && !MatchesExpectedAssetType(reference, target))
                {
                    diagnostics.Add(TypeMismatchIssue(consumer, reference, target));
                    continue;
                }

                var candidate = found.asset is null || this.ProjectSourceWins(found.asset.Identity, found.root)
                    ? await this.ResolveProjectKeyAsync(dependency, inspector as ICookedAssetKeyProvider, operationRoot, artifacts, cancellationToken).ConfigureAwait(false) : null;
                if (candidate is not null && (found.asset is null || this.ProjectHasPriority(found.root)))
                {
                    this.AddProjectSource(candidate, projects, importedOutputs);
                }
                else if (found.asset is not null)
                {
                    if (this.projectIdentities?.IsPending == true && this.ProjectSourceWins(found.asset.Identity, found.root))
                    {
                        diagnostics.Add(PendingIdentitiesIssue(consumer));
                    }

                    pending.Enqueue(found);
                }
                else
                {
                    diagnostics.Add(this.projectIdentities?.IsPending == true
                        ? PendingIdentitiesIssue(consumer)
                        : Issue(consumer, "asset_cook.library_dependency_missing", $"Library asset '{asset.Identity}' requires missing asset '{dependency}'."));
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
        foreach (var (name, _, message) in this.unreadable)
        {
            foreach (var consumer in graph.Assets.Where(input => graph.NativeReferences.GetValueOrDefault(input.AssetUri, []).Length != 0))
            {
                diagnostics.Add(Issue(consumer, "asset_cook.library_unavailable", $"Cannot read cooked library '{name}': {message}"));
            }
        }

        var requested = graph.NativeReferences.Values.SelectMany(static values => values).Concat(graph.PublishedReferences).Distinct();
        var priority = this.order.Select((source, index) => (source, index)).ToArray();
        var projectPriority = priority.Single(static item => item.source.Kind == CookedContentSourceKind.ProjectOutput).index;
        const int EnginePriority = -1;
        foreach (var uri in requested)
        {
            var match = this.roots.AsEnumerable().Reverse().Select(root => (root, record: root.Assets.FirstOrDefault(record => record.Identity == uri))).FirstOrDefault(static item => item.record is not null);
            var owned = HasProjectOwner(graph, uri) || this.knownProjectOutputs.Contains(uri);
            if (match.record?.Cooked is { } asset)
            {
                var libraryPriority = match.root.IsEngineContent ? EnginePriority
                    : priority.Single(item => string.Equals(item.source.Name, match.root.Name, StringComparison.Ordinal)).index;
                if (owned && projectPriority > libraryPriority)
                {
                    continue;
                }

                if (AssetFileTypes.Find(uri.Name) is { Category: AssetFileCategory.Cooked, AssetType: not AssetType.Unknown and var expected }
                    && asset.AssetType != (byte)expected)
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

        return graph with { Diagnostics = diagnostics.ToImmutable() };

        void AddIssue(AssetPath uri, string code, string message)
        {
            foreach (var consumer in graph.NativeReferences.Where(pair => pair.Value.Contains(uri)).Select(pair => graph.Assets.Single(input => input.AssetUri == pair.Key)))
            {
                diagnostics.Add(Issue(consumer, code, message));
            }
        }
    }

    /// <summary>Orders the native lookup roots from lowest to highest priority, the engine content pak first.</summary>
    /// <param name="projectRoots">The operation's staged and reusable project roots.</param>
    /// <returns>The roots native cooks resolve references against.</returns>
    public IReadOnlyList<string> OrderRoots(IEnumerable<string> projectRoots)
    {
        var declared = this.order.SelectMany(source => source.Kind == CookedContentSourceKind.ProjectOutput ? projectRoots.Order(StringComparer.Ordinal)
            : this.roots.Where(root => !root.IsEngineContent && string.Equals(root.Name, source.Name, StringComparison.Ordinal)).Select(static root => root.Path));
        var paths = this.roots.Where(static root => root.IsEngineContent).Select(static root => root.Path).Concat(declared);
        return paths.Reverse().DistinctBy(FileSystemPath.From).Reverse().ToArray();
    }

    /// <summary>Validates native container structure before consumers can use library data.</summary>
    /// <param name="native">The native container validator.</param>
    /// <param name="cancellationToken">Cancels native validation.</param>
    /// <returns>The asynchronous validation.</returns>
    public async Task ValidateNativeAsync(IEngineContentPipelineApi native, CancellationToken cancellationToken)
    {
        // The engine content pak is an SDK artifact: the cooking artifact lease verifies its bytes.
        foreach (var root in this.roots.Where(static root => root.Reader is not null))
        {
            var inventory = await root.Reader!.ReadInventoryAsync(native, cancellationToken).ConfigureAwait(false);
            var result = inventory.ToValidation(root.Path);
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
            _ = root.Reader?.GetFiles();
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var root in this.roots)
        {
            root.Reader?.Dispose();
            root.PakLease?.Dispose();
        }
    }

    internal static bool MatchesExpectedAssetType(CookedKeyReference reference, CookedAssetMetadata target)
        => reference.TargetKind == CookedKeyReferenceTargetKind.Texture
            ? target.AssetType == (byte)AssetType.Unknown
            : reference.ExpectedAssetType == 0 || target.AssetType == reference.ExpectedAssetType;

    /// <summary>Uses the same saved low-to-high source order as runtime mounting; unreadable libraries are left out.</summary>
    /// <param name="projectRoots">The operation's staged and reusable project roots.</param>
    /// <returns>Native lookup roots ordered from lowest to highest priority.</returns>
    internal ImmutableArray<CookPublicationRoot> OrderBindings(IEnumerable<CookPublicationRoot> projectRoots)
    {
        var owned = projectRoots.OrderBy(static root => root.Name, StringComparer.Ordinal).ToArray();
        var result = new List<CookPublicationRoot>();
        foreach (var source in this.order)
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput)
            {
                result.AddRange(owned);
            }
            else if (this.roots.FirstOrDefault(root => !root.IsEngineContent && string.Equals(root.Name, source.Name, StringComparison.Ordinal)) is { } library)
            {
                result.Add(new(CookPublicationRootOwner.Library, library.Name, library.SourceKey, library.Fingerprint, library.Path) { Container = library.Container });
            }
        }

        return [.. result.AsEnumerable().Reverse().DistinctBy(root => FileSystemPath.From(root.ResolvePath(this.projectRoot))).Reverse()];
    }

    private static bool HasProjectOwner(CookDependencyGraph graph, AssetPath uri)
        => graph.Assets.Any(input => input.OwnsOutput(uri.VirtualPath));

    private static DiagnosticRecord IncompleteInspectionIssue(ContentCookInput consumer, CookedAssetDependencies? metadata)
        => metadata is null
            ? Issue(consumer, "asset_cook.library_inspection_required", "Library dependencies are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning }
            : Issue(consumer, "asset_cook.library_inspection_failed", metadata.Diagnostic ?? "Library dependencies could not be inspected.");

    private static DiagnosticRecord TypeMismatchIssue(ContentCookInput consumer, CookedKeyReference reference, CookedAssetMetadata target)
        => Issue(
            consumer,
            "asset_cook.library_type_mismatch",
            $"Library asset key '{reference.AssetKey}' has type {target.AssetType}, but the native reference expects type {reference.ExpectedAssetType}.");

    private static DiagnosticRecord PendingIdentitiesIssue(ContentCookInput consumer)
        => Issue(consumer, "asset_cook.library_inspection_required", "Project asset identities are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning };

    private static DiagnosticRecord Issue(ContentCookInput consumer, string code, string message)
        => new() { OperationId = Guid.Empty, Domain = FailureDomain.AssetCook, Severity = DiagnosticSeverity.Error, Code = code, Message = message, AffectedPath = consumer.SourceAbsolutePath.FullPath, AffectedVirtualPath = consumer.AssetUri.VirtualPath };

    private Queue<(Root root, AssetRecord asset)> CreateReferenceQueue(IReadOnlyList<AssetPath> references)
    {
        var pending = new Queue<(Root root, AssetRecord asset)>();
        foreach (var uri in references)
        {
            var (root, asset) = this.FindUri(uri);
            if (asset is not null && !this.ProjectSourceWins(uri, root))
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

    private async Task<AssetPath?> ResolveProjectKeyAsync(string key, ICookedAssetKeyProvider? provider, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        this.projectIdentities ??= await ProjectAssetKeyIndex.ReadAsync(this.project, this.knownProjectOutputs, cancellationToken).ConfigureAwait(false);
        if (provider is not null)
        {
            _ = await this.projectIdentities.EnsureAsync(provider, operationRoot, cancellationToken, artifacts).ConfigureAwait(false);
        }

        return this.projectIdentities.Resolve(key);
    }

    // A project key wins over the library copy: the project source is cooked in its place.
    private void AddProjectSource(AssetPath candidate, Dictionary<AssetPath, ContentCookInput> projects, HashSet<AssetPath> importedOutputs)
    {
        var source = this.resolveImported?.Invoke(candidate) ?? CookInputResolver.Resolve(this.project, candidate, ContentCookInputRole.Dependency);
        projects[source.AssetUri] = source;
        if (source.Kind == ContentCookAssetKind.ForeignSource)
        {
            _ = importedOutputs.Add(candidate);
        }
    }

    private bool ProjectHasPriority(Root root)
        => this.order.Last(source => source.Kind == CookedContentSourceKind.ProjectOutput
            || (source.Kind == CookedContentSourceKind.LocalFolder && string.Equals(source.Name, root.Name, StringComparison.Ordinal))).Kind == CookedContentSourceKind.ProjectOutput;

    private bool ProjectSourceWins(AssetPath uri, Root root)
        => this.ProjectHasPriority(root) && (this.resolveImported?.Invoke(uri) is not null
            || CookInputResolver.HasProjectSource(this.project, uri));

    private (Root root, AssetRecord asset) FindUri(AssetPath uri)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => asset.Identity == uri).Select(asset => (root, asset))).FirstOrDefault();

    private (Root root, AssetRecord asset) FindKey(string key)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => string.Equals(asset.Cooked?.AssetKey.ToString(), key, StringComparison.OrdinalIgnoreCase)).Select(asset => (root, asset))).FirstOrDefault();

    /// <summary>A declared library that could not be read.</summary>
    /// <param name="Name">The library's mount name.</param>
    /// <param name="Path">The library's folder or pak file.</param>
    /// <param name="Reason">Why it could not be read.</param>
    public sealed record UnreadableLibrary(string Name, string Path, string Reason);

    /// <summary>A library asset preferred over the project's source, in the root that holds it.</summary>
    /// <param name="CookedRoot">The physical cooked root.</param>
    /// <param name="Asset">The asset's index entry.</param>
    public sealed record PreferredLibraryAsset(string CookedRoot, AssetRecord Asset);

    // Every runtime mounts the engine content pak as its permanent base, so it never joins publication bindings.
    private async Task AddEngineContentAsync(IEngineContentPak engineContent, CancellationToken cancellationToken)
    {
        try
        {
            var document = await engineContent.ReadAsync(cancellationToken).ConfigureAwait(false);
            var root = await this.CreatePakRootAsync(AssetPath.Engine.Mount, engineContent.Path, document, cancellationToken).ConfigureAwait(false);
            this.roots.Add(root with { IsEngineContent = true });
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            this.unreadable.Add(new(AssetPath.Engine.Mount, engineContent.Path.FullPath, failure.Message));
        }
    }

    // A declared folder without an index holds no cooked library yet and is skipped.
    private async Task AddLooseFolderAsync(LocalFolderMount mount, CancellationToken cancellationToken)
    {
        var path = mount.Root.FullPath;
        if (!File.Exists(Path.Combine(path, "container.index.bin")))
        {
            return;
        }

        CookOutputReadLease? files = null;
        try
        {
            CookOutputLease.RejectReparsePoint(path);
            files = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
            var read = await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false);
            if (read is CookedIndexRead.Stale stale)
            {
                this.unreadable.Add(new(mount.Name, path, $"it was cooked in index format {stale.FormatVersion}; recook it"));
                return;
            }

            var index = read.RequireCurrent(path);
            var records = await index.ReadRecordsAsync(cancellationToken).ConfigureAwait(false);
            var fingerprint = index.Fingerprint;
            var metadata = await CookedDependencyCache.ReadAsync(this.projectRoot, fingerprint, records, cancellationToken).ConfigureAwait(false);
            this.roots.Add(new(mount.Name, path, records, fingerprint, files) { SourceKey = index.Index.SourceGuid, Dependencies = metadata });
            files = null;
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            this.unreadable.Add(new(mount.Name, path, failure.Message));
        }
        finally
        {
            if (files is not null)
            {
                await files.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    // The engine verifies a pak's integrity when it mounts it; the lease keeps its bytes fixed until then.
    private async Task AddPakFileAsync(PakFileMount mount, CancellationToken cancellationToken)
    {
        PakReadLease? lease = null;
        try
        {
            lease = await PakReadLease.AcquireAsync(mount.Path, cancellationToken).ConfigureAwait(false);
            var root = await this.CreatePakRootAsync(mount.Name, mount.Path, lease.Document, cancellationToken).ConfigureAwait(false);
            this.roots.Add(root with { Container = CookPublicationRootContainer.PakFile, PakLease = lease });
            lease = null;
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            this.unreadable.Add(new(mount.Name, mount.Path.FullPath, failure.Message));
        }
        finally
        {
            lease?.Dispose();
        }
    }

    // A pak's catalog digest identifies its content; its dependencies come from the native report, as for folders.
    private async Task<Root> CreatePakRootAsync(string name, FileSystemPath path, PakDocument document, CancellationToken cancellationToken)
    {
        var catalog = new PakAssetCatalog(document, path);
        var records = await catalog.QueryAsync(new(AssetQueryScope.All), cancellationToken).ConfigureAwait(false);
        var metadata = await CookedDependencyCache.ReadAsync(this.projectRoot, document.CatalogDigest, records, cancellationToken).ConfigureAwait(false);
        return new(name, path.FullPath, records, document.CatalogDigest, Reader: null) { SourceKey = document.SourceIdentity, Dependencies = metadata };
    }

    /// <summary>One library: a declared loose cooked folder, a declared pak file, or the engine content pak.</summary>
    /// <param name="Name">The library's name: a declared mount, or the engine mount.</param>
    /// <param name="Path">The physical container.</param>
    /// <param name="Assets">The assets the container lists.</param>
    /// <param name="Fingerprint">The container's content identity.</param>
    /// <param name="Reader">The protected files of a loose folder; null for a pak.</param>
    private sealed record Root(string Name, string Path, IReadOnlyList<AssetRecord> Assets, string Fingerprint, CookOutputReadLease? Reader)
    {
        public required Guid SourceKey { get; init; }

        /// <summary>Gets a value indicating whether this is the engine content pak, beneath every declared library.</summary>
        public bool IsEngineContent { get; init; }

        /// <summary>Gets how the library stores its content.</summary>
        public CookPublicationRootContainer Container { get; init; } = CookPublicationRootContainer.LooseFolder;

        /// <summary>Gets the shared read handle of a declared pak file; null for folders and for the engine content pak, which the cooking artifact lease protects.</summary>
        public PakReadLease? PakLease { get; init; }

        public CookedDependencyReport? Dependencies { get; set; }
    }
}
