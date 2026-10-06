// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Resolves project ownership around native dependency frontiers.</summary>
internal sealed class CookDependencyDiscovery(
    ICookSourceFactsProvider analyzer,
    Func<Uri, ContentCookInput?>? resolveImported = null,
    Func<string, ContentCookInput?>? resolveBufferOwner = null,
    Func<Uri, bool>? preferCookedReference = null,
    Func<ContentCookInput, IReadOnlyList<Uri>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences = null)
{
    public Task<CookDependencyGraph> DiscoverAsync(ProjectContext project, IReadOnlyList<ContentCookInput> roots, CancellationToken cancellationToken)
        => this.DiscoverAsync(project, roots, preparedImport: null, cancellationToken);

    internal async Task<CookDependencyGraph> DiscoverAsync(ProjectContext project, IReadOnlyList<ContentCookInput> roots, CookDependencyGraph? preparedImport, CancellationToken cancellationToken)
    {
        var discovery = new Discovery(project, analyzer, resolveImported, resolveBufferOwner, preferCookedReference, expandCookedReferences);
        if (preparedImport is not null)
        {
            discovery.AddPreparedImport(preparedImport);
        }

        foreach (var input in roots)
        {
            discovery.AddAsset(input);
        }

        return await discovery.RunAsync(cancellationToken).ConfigureAwait(false);
    }

    internal static string FingerprintImportedContent(IEnumerable<CookSnapshotInput> inputs)
        => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(inputs.OrderBy(static file => file.RelativePath, StringComparer.Ordinal)
            .Select(static file => new { file.RelativePath, file.DiscoveryHash, file.Kind, Metadata = file.ProbeShape }))));

    private sealed class Discovery(ProjectContext project, ICookSourceFactsProvider analyzer,
        Func<Uri, ContentCookInput?>? resolveImported,
        Func<string, ContentCookInput?>? resolveBufferOwner,
        Func<Uri, bool>? preferCookedReference,
        Func<ContentCookInput, IReadOnlyList<Uri>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences)
    {
        private readonly Dictionary<string, ContentCookInput> assets = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<string, CookSnapshotInput> files = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<Uri, ImmutableArray<Uri>> dependencies = [];
        private readonly Dictionary<Uri, ImmutableArray<Uri>> nativeReferences = [];
        private readonly Dictionary<Uri, ImmutableArray<CookedDependencySnapshot>> cookedDependencies = [];
        private readonly Dictionary<Uri, HashSet<string>> fileDependencies = [];
        private readonly HashSet<Uri> builtins = [];
        private readonly HashSet<Uri> published = [];
        private readonly HashSet<Uri> importedReferences = [];
        private readonly Queue<ContentCookInput> pending = new();
        private readonly List<DiagnosticRecord> diagnostics = [];
        private readonly Dictionary<Uri, ImportedSourceDependencyState> imported = [];
        private readonly Dictionary<Uri, SceneDescriptorGenerationResult> scenes = [];
        private readonly Dictionary<Uri, ContentImportJob> jobs = [];
        private readonly Dictionary<Uri, CookSourceFacts> sourceFacts = [];
        private readonly HashSet<Uri> requiresAnalysis = [];
        private readonly Dictionary<Uri, ContentCookInput> generatedSources = [];
        private readonly Dictionary<string, NativeCapturedInput> generatedInputs = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<string, Uri> builtinOwners = [with(StringComparer.Ordinal)];
        private readonly Dictionary<string, ContentCookInput> bufferOwners = [with(StringComparer.Ordinal)];
        private readonly Dictionary<Uri, ImmutableArray<Uri>> resourceDependencies = [];

        public void AddPreparedImport(CookDependencyGraph prepared)
        {
            var source = prepared.Assets.Single();
            this.assets.Add(Path.GetFullPath(source.SourceAbsolutePath), source);
            this.dependencies.Add(source.AssetUri, prepared.Dependencies[source.AssetUri]);
            this.fileDependencies.Add(source.AssetUri, prepared.FileDependencies[source.AssetUri].ToHashSet(StringComparer.Ordinal));
            this.imported.Add(source.AssetUri, prepared.ImportedSources[source.AssetUri]);
            this.importedReferences.UnionWith(prepared.ImportedReferences);
            foreach (var file in prepared.Files)
            {
                this.files.Add(Path.GetFullPath(Path.Combine(project.ProjectRoot, file.RelativePath)), file);
            }
        }

        public void AddAsset(ContentCookInput input)
        {
            _ = this.RelativePath(input.SourceAbsolutePath);
            if (this.assets.TryAdd(Path.GetFullPath(input.SourceAbsolutePath), input))
            {
                this.pending.Enqueue(input);
                CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Preparing)));
            }
        }

        public async Task<CookDependencyGraph> RunAsync(CancellationToken cancellationToken)
        {
            while (this.pending.Count != 0)
            {
                var frontier = this.pending.ToArray();
                this.pending.Clear();
                var analyzed = await analyzer.ReadAsync(frontier, cancellationToken).ConfigureAwait(false);
                this.diagnostics.AddRange(analyzed.Diagnostics);
                foreach (var (path, owner) in analyzed.BuiltinOwners)
                {
                    this.builtinOwners[path] = owner;
                }

                foreach (var source in analyzed.GeneratedSources)
                {
                    this.generatedSources[source.AssetUri] = source;
                    if (source.OutputVirtualPath is { } path)
                    {
                        this.builtinOwners[path] = source.AssetUri;
                    }
                }

                foreach (var input in analyzed.GeneratedInputs)
                {
                    this.generatedInputs[input.LogicalPath] = input;
                }

                foreach (var source in analyzed.Sources)
                {
                    foreach (var output in source.Outputs.Where(static output => string.Equals(output.Kind, "buffer", StringComparison.Ordinal)))
                    {
                        if (this.bufferOwners.TryGetValue(output.VirtualPath, out var owner) && owner.AssetUri != source.Input.AssetUri)
                        {
                            throw new InvalidDataException($"Multiple sources claim buffer '{output.VirtualPath}'.");
                        }

                        this.bufferOwners[output.VirtualPath] = source.Input;
                    }
                }

                foreach (var source in analyzed.Sources)
                {
                    await this.ApplySourceAsync(source, cancellationToken).ConfigureAwait(false);
                }
            }

            return new(
                [.. this.assets.Values.OrderBy(static input => input.SourceRelativePath, StringComparer.Ordinal)],
                [.. this.files.Values.OrderBy(static input => input.RelativePath, StringComparer.Ordinal)],
                this.dependencies.ToImmutableDictionary(),
                this.fileDependencies.ToImmutableDictionary(static pair => pair.Key, static pair => pair.Value.Order(StringComparer.Ordinal).ToImmutableArray()),
                [.. this.builtins.OrderBy(static uri => uri.AbsoluteUri, StringComparer.Ordinal)],
                [.. this.published.OrderBy(static uri => uri.AbsoluteUri, StringComparer.Ordinal)],
                [.. this.diagnostics])
            {
                ImportedSources = this.imported.ToImmutableDictionary(), ImportedReferences = [.. this.importedReferences],
                NativeReferences = this.nativeReferences.ToImmutableDictionary(), CookedDependencies = this.cookedDependencies.ToImmutableDictionary(),
                SceneDescriptors = this.scenes.ToImmutableDictionary(), NativeJobs = this.jobs.ToImmutableDictionary(),
                GeneratedSources = [.. this.generatedSources.Values], GeneratedInputs = [.. this.generatedInputs.Values],
                SourceFacts = this.sourceFacts.ToImmutableDictionary(), SourcesNeedingAnalysis = this.requiresAnalysis.ToImmutableHashSet(),
                ResourceDependencies = this.resourceDependencies.ToImmutableDictionary(),
            };
        }

        private async Task ApplySourceAsync(CookSourceFacts source, CancellationToken cancellationToken)
        {
            var input = source.Input;
            this.assets[Path.GetFullPath(input.SourceAbsolutePath)] = input;
            this.sourceFacts[input.AssetUri] = source;
            if (source.Job is { } job)
            {
                this.jobs[input.AssetUri] = job;
            }

            if (source.RequiresAnalysis)
            {
                _ = this.requiresAnalysis.Add(input.AssetUri);
            }

            this.fileDependencies[input.AssetUri] = [with(StringComparer.Ordinal)];
            foreach (var file in source.Files)
            {
                var path = Path.GetFullPath(file.SourcePath);
                _ = this.fileDependencies[input.AssetUri].Add(file.RelativePath);
                if (this.files.TryGetValue(path, out var existing))
                {
                    var selected = (int)existing.Kind <= (int)file.Kind ? existing : file;
                    this.files[path] = selected with
                    {
                        AssetUri = existing.AssetUri ?? file.AssetUri,
                        NativeObservations = [.. existing.NativeObservations, .. file.NativeObservations],
                        Metadata = selected.Metadata ?? existing.Metadata ?? file.Metadata,
                    };
                }
                else
                {
                    this.files.Add(path, file);
                }
            }

            if (source.Scene is { } scene)
            {
                this.scenes[input.AssetUri] = scene;
            }

            if (input.Kind == ContentCookAssetKind.ForeignSource && !source.RequiresAnalysis)
            {
                var primary = source.Files.Single(file => file.AssetUri == input.AssetUri);
                this.imported[input.AssetUri] = new(
                    primary.DiscoveryHash,
                    [.. source.Files.Where(file => !string.Equals(file.RelativePath, input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, StringComparison.OrdinalIgnoreCase))
                        .Select(static file => file.RelativePath).Order(StringComparer.Ordinal)])
                {
                    ContentFingerprint = FingerprintImportedContent(source.Files),
                };
            }

            var localOutputs = source.Outputs.Select(static output => output.VirtualPath).ToHashSet(StringComparer.Ordinal);
            var external = source.References.Where(reference => !localOutputs.Contains(reference.VirtualPath)).ToArray();
            var references = external.Where(static reference => !string.Equals(reference.Kind, "buffer", StringComparison.Ordinal))
                .Select(static reference => new Uri($"{AssetUris.Scheme}://{string.Join('/', reference.VirtualPath.Split('/').Select(Uri.EscapeDataString))}"))
                .Distinct().ToArray();
            this.nativeReferences[input.AssetUri] = [.. references];
            var resolved = references.Select(this.ResolveReference).ToHashSet();
            var bufferDependencies = new HashSet<Uri>();
            foreach (var reference in external.Where(static reference => string.Equals(reference.Kind, "buffer", StringComparison.Ordinal)))
            {
                var owner = this.bufferOwners.GetValueOrDefault(reference.VirtualPath) ?? resolveBufferOwner?.Invoke(reference.VirtualPath);
                if (owner is null)
                {
                    // Native linking validates existing local sidecars and rejects foreign-root indices.
                    continue;
                }

                if (!string.Equals(owner.MountName, input.MountName, StringComparison.Ordinal))
                {
                    throw new InvalidDataException($"Raw buffer '{reference.VirtualPath}' belongs to another cooked root. Reference its geometry asset or import the buffer source into this root.");
                }

                this.AddAsset(owner);
                _ = resolved.Add(owner.AssetUri);
                _ = bufferDependencies.Add(owner.AssetUri);
            }

            this.resourceDependencies[input.AssetUri] = [.. bufferDependencies];
            if (expandCookedReferences is not null)
            {
                var expansion = await expandCookedReferences(input, references, cancellationToken).ConfigureAwait(false);
                this.cookedDependencies[input.AssetUri] = expansion.Libraries;
                this.diagnostics.AddRange(expansion.Diagnostics);
                this.importedReferences.UnionWith(expansion.ImportedOutputs);
                foreach (var dependency in expansion.ProjectInputs)
                {
                    this.AddAsset(dependency);
                    _ = resolved.Add(dependency.AssetUri);
                }
            }

            this.dependencies[input.AssetUri] = [.. resolved.OrderBy(static uri => uri.AbsoluteUri, StringComparer.Ordinal)];
        }

        private Uri ResolveReference(Uri uri)
        {
            if (!string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Cook dependency '{uri}' must be an asset identity.");
            }

            if (this.builtinOwners.TryGetValue(Uri.UnescapeDataString(uri.AbsolutePath), out var generated))
            {
                _ = this.builtins.Add(generated);
                return generated;
            }

            if (uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase))
            {
                _ = this.builtins.Add(uri);
                return uri;
            }

            if (preferCookedReference?.Invoke(uri) == true)
            {
                var nativeUri = uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? new Uri(uri.AbsoluteUri[..^5]) : uri;
                _ = this.published.Add(nativeUri);
                return nativeUri;
            }

            if (Path.GetExtension(uri.AbsolutePath).ToUpperInvariant() is ".OSCRIPT" or ".OIACT" or ".OIMAP" or ".OPSCENE")
            {
                _ = this.published.Add(uri);
                return uri;
            }

            if (resolveImported?.Invoke(uri) is { } owner)
            {
                _ = this.importedReferences.Add(uri);
                this.AddAsset(owner);
                return this.assets[Path.GetFullPath(owner.SourceAbsolutePath)].AssetUri;
            }

            if (!CookInputResolver.IsAuthoringUri(project, uri) && !uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
            {
                _ = this.published.Add(uri);
                return uri;
            }

            var input = CookInputResolver.Resolve(project, uri, ContentCookInputRole.Dependency);
            if (File.Exists(input.SourceAbsolutePath) || uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
            {
                this.AddAsset(input);
                return this.assets[Path.GetFullPath(input.SourceAbsolutePath)].AssetUri;
            }

            _ = this.published.Add(uri);
            return uri;
        }

        private string RelativePath(string path)
        {
            var relative = Path.GetRelativePath(project.ProjectRoot, Path.GetFullPath(path)).Replace('\\', '/');
            return Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal)
                ? throw new InvalidDataException($"Cook dependency '{path}' is outside the project. Retain it in the project before cooking.")
                : relative;
        }
    }
}
