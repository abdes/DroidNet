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

    /// <summary>Reads library indexes and protects the full container inputs without starting a native worker.</summary>
    /// <param name="project">The project declaring the ordered libraries.</param>
    /// <param name="graph">The saved dependency closure.</param>
    /// <param name="cancellationToken">Cancels acquisition and hashing.</param>
    /// <returns>The selected library readers and lookup facts.</returns>
    public static async Task<CookedLibraryReadSet> AcquireAsync(ProjectContext project, CookDependencyGraph graph, CancellationToken cancellationToken)
    {
        var result = new CookedLibraryReadSet { order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder), projectRoot = project.ProjectRoot };
        if (graph.NativeReferences.Values.All(static references => references.IsEmpty) && graph.PublishedReferences.IsEmpty)
        {
            return result;
        }

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
        var dependencies = graph.CookedDependencies.Values.OrderBy(static dependency => dependency.AssetUri.AbsoluteUri, StringComparer.Ordinal).ToImmutableArray();
        return dependencies.IsEmpty ? snapshot : snapshot with
        {
            CookedDependencies = dependencies,
            InputIdentity = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new { snapshot.InputIdentity, Libraries = dependencies }))),
        };
    }

    /// <summary>Inspects reachable libraries on cache misses before native consumers execute.</summary>
    /// <param name="graph">The saved authoring references.</param>
    /// <param name="inspector">The native dependency decoder.</param>
    /// <param name="operationRoot">Private native scratch space.</param>
    /// <param name="artifacts">Borrowed native artifact ownership.</param>
    /// <param name="cancellationToken">Cancels metadata inspection.</param>
    /// <returns>Completion after reachable dependency reports are cached.</returns>
    public async Task PopulateDependenciesAsync(CookDependencyGraph graph, ICookedDependencyInspector inspector, string operationRoot, Oxygen.Managed.Core.Compatibility.NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        var pending = new Queue<(Root root, AssetRecord asset)>();
        foreach (var uri in graph.NativeReferences.Values.SelectMany(static values => values).Concat(graph.PublishedReferences).Distinct())
        {
            var match = this.FindUri(uri);
            if (match.asset is not null)
            {
                pending.Enqueue(match);
            }
        }

        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (pending.TryDequeue(out var current))
        {
            var key = current.asset.Cooked!.AssetKey.ToString();
            if (!visited.Add(key))
            {
                continue;
            }

            current.root.Dependencies ??= await CookedDependencyCache.EnsureAsync(this.projectRoot, current.root.Path, current.root.Fingerprint, current.root.Assets, inspector, operationRoot, cancellationToken, artifacts).ConfigureAwait(false);
            foreach (var dependency in current.root.Dependencies.Assets[key].Dependencies)
            {
                var resolved = this.FindKey(dependency);
                if (resolved.asset is not null)
                {
                    pending.Enqueue(resolved);
                }
            }
        }
    }

    /// <summary>Attaches selected library inputs and reports required references that remain unavailable.</summary>
    /// <param name="graph">The discovered authoring closure.</param>
    /// <returns>The closure with selected library dependencies and scoped issues.</returns>
    public CookDependencyGraph Apply(CookDependencyGraph graph)
    {
        var bindings = ImmutableDictionary.CreateBuilder<Uri, CookedDependencySnapshot>();
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
            var owned = graph.Assets.Any(input => string.Equals(input.OutputVirtualPath, uri.AbsolutePath, StringComparison.Ordinal)
                || (input.Kind == ContentCookAssetKind.ForeignSource && input.OutputVirtualPath is { } prefix && uri.AbsolutePath.StartsWith(prefix, StringComparison.Ordinal)));
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

                bindings[uri] = new(uri, match.root.Name, match.root.Path, asset.AssetKey.ToString(), asset.AssetType, match.root.Fingerprint);
            }
            else if (graph.PublishedReferences.Contains(uri) && !owned)
            {
                AddIssue(uri, "asset_cook.library_reference_missing", $"Cooked reference '{uri}' was not found in the declared libraries.");
            }
        }

        var references = this.ExpandDependencies(graph, bindings, diagnostics);

        var usedRoots = bindings.Values.Select(static binding => binding.RootPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var unused in this.roots.Where(root => !usedRoots.Contains(root.Path)).ToArray())
        {
            unused.Reader.Dispose();
            _ = this.roots.Remove(unused);
        }

        return graph with { CookedDependencies = bindings.ToImmutable(), NativeReferences = references, Diagnostics = diagnostics.ToImmutable() };

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

    private static DiagnosticRecord Issue(ContentCookInput consumer, string code, string message)
        => new() { OperationId = Guid.Empty, Domain = FailureDomain.AssetCook, Severity = DiagnosticSeverity.Error, Code = code, Message = message, AffectedPath = consumer.SourceAbsolutePath, AffectedVirtualPath = consumer.AssetUri.AbsolutePath };

    private ImmutableDictionary<Uri, ImmutableArray<Uri>> ExpandDependencies(CookDependencyGraph graph, ImmutableDictionary<Uri, CookedDependencySnapshot>.Builder bindings, ImmutableArray<DiagnosticRecord>.Builder diagnostics)
    {
        var references = graph.NativeReferences.ToBuilder();
        foreach (var (consumer, direct) in graph.NativeReferences)
        {
            var closure = direct.ToHashSet();
            var pending = new Queue<Uri>(direct);
            var input = graph.Assets.Single(asset => asset.AssetUri == consumer);
            while (pending.TryDequeue(out var uri))
            {
                if (!bindings.TryGetValue(uri, out var binding))
                {
                    continue;
                }

                var root = this.roots.Single(root => string.Equals(root.Path, binding.RootPath, StringComparison.OrdinalIgnoreCase));
                if (root.Dependencies is null)
                {
                    diagnostics.Add(Issue(input, "asset_cook.library_inspection_required", "Library dependencies are awaiting inspection.") with { Severity = DiagnosticSeverity.Warning });
                    continue;
                }

                var metadata = root.Dependencies.Assets[binding.AssetKey];
                if (!metadata.Complete)
                {
                    diagnostics.Add(Issue(input, "asset_cook.library_inspection_failed", metadata.Diagnostic ?? "Library dependencies could not be inspected."));
                    continue;
                }

                foreach (var key in metadata.Dependencies)
                {
                    var resolved = this.FindKey(key);
                    if (resolved.asset?.Cooked is not { } asset)
                    {
                        diagnostics.Add(Issue(input, "asset_cook.library_dependency_missing", $"Library asset '{uri}' requires missing asset '{key}'."));
                        continue;
                    }

                    var dependency = resolved.asset.Uri;
                    bindings[dependency] = new(dependency, resolved.root.Name, resolved.root.Path, key, asset.AssetType, resolved.root.Fingerprint);
                    if (closure.Add(dependency))
                    {
                        pending.Enqueue(dependency);
                    }
                }
            }

            references[consumer] = [.. closure];
        }

        return references.ToImmutable();
    }

    private (Root root, AssetRecord asset) FindUri(Uri uri)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => asset.Uri == uri).Select(asset => (root, asset))).FirstOrDefault();

    private (Root root, AssetRecord asset) FindKey(string key)
        => this.roots.AsEnumerable().Reverse().SelectMany(root => root.Assets.Where(asset => string.Equals(asset.Cooked?.AssetKey.ToString(), key, StringComparison.OrdinalIgnoreCase)).Select(asset => (root, asset))).FirstOrDefault();

    private sealed record Root(string Name, string Path, IReadOnlyList<AssetRecord> Assets, string Fingerprint, CookOutputReadLease Reader)
    {
        public CookedDependencyReport? Dependencies { get; set; }
    }
}
