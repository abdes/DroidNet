// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Resolves project ownership around native dependency frontiers.</summary>
internal sealed class CookDependencyDiscovery(
    ICookSourceFactsProvider analyzer,
    Func<AssetPath, ContentCookInput?>? resolveImported = null,
    Func<string, ContentCookInput?>? resolveProducer = null,
    Func<AssetPath, bool>? preferCookedReference = null,
    Func<ContentCookInput, IReadOnlyList<AssetPath>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences = null)
{
    public Task<CookDependencyGraph> DiscoverAsync(ProjectContext project, IReadOnlyList<ContentCookInput> roots, CancellationToken cancellationToken)
        => this.DiscoverAsync(project, roots, preparedImport: null, cancellationToken);

    internal async Task<CookDependencyGraph> DiscoverAsync(ProjectContext project, IReadOnlyList<ContentCookInput> roots, CookDependencyGraph? preparedImport, CancellationToken cancellationToken)
    {
        var discovery = new Discovery(project, analyzer, resolveImported, resolveProducer, preferCookedReference, expandCookedReferences);
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
        Func<AssetPath, ContentCookInput?>? resolveImported,
        Func<string, ContentCookInput?>? resolveProducer,
        Func<AssetPath, bool>? preferCookedReference,
        Func<ContentCookInput, IReadOnlyList<AssetPath>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences)
    {
        private readonly Dictionary<FileSystemPath, ContentCookInput> assets = [];
        private readonly Dictionary<FileSystemPath, CookSnapshotInput> files = [];
        private readonly Dictionary<AssetPath, ImmutableArray<AssetPath>> dependencies = [];
        private readonly Dictionary<AssetPath, ImmutableArray<AssetPath>> nativeReferences = [];
        private readonly Dictionary<AssetPath, ImmutableArray<CookedDependencySnapshot>> cookedDependencies = [];
        private readonly Dictionary<AssetPath, HashSet<string>> fileDependencies = [];
        private readonly HashSet<AssetPath> published = [];
        private readonly HashSet<AssetPath> importedReferences = [];
        private readonly Queue<ContentCookInput> pending = new();
        private readonly List<DiagnosticRecord> diagnostics = [];
        private readonly Dictionary<AssetPath, ImportedSourceDependencyState> imported = [];
        private readonly Dictionary<AssetPath, SceneDescriptorGenerationResult> scenes = [];
        private readonly Dictionary<AssetPath, ContentImportJob> jobs = [];
        private readonly Dictionary<AssetPath, CookSourceFacts> sourceFacts = [];
        private readonly HashSet<AssetPath> requiresAnalysis = [];
        private readonly Dictionary<string, NativeCapturedInput> generatedInputs = [with(StringComparer.OrdinalIgnoreCase)];
        // Each identity has one producing source; identities differing only in case are one file in a cooked root.
        private readonly Dictionary<string, ContentCookInput> producers = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<AssetPath, ImmutableArray<AssetPath>> resourceDependencies = [];

        public void AddPreparedImport(CookDependencyGraph prepared)
        {
            var source = prepared.Assets.Single();
            this.assets.Add(source.SourceAbsolutePath, source);
            this.dependencies.Add(source.AssetUri, prepared.Dependencies[source.AssetUri]);
            this.fileDependencies.Add(source.AssetUri, prepared.FileDependencies[source.AssetUri].ToHashSet(StringComparer.Ordinal));
            this.imported.Add(source.AssetUri, prepared.ImportedSources[source.AssetUri]);
            this.importedReferences.UnionWith(prepared.ImportedReferences);
            foreach (var file in prepared.Files)
            {
                this.files.Add(FileSystemPath.From(Path.Combine(project.ProjectRoot.FullPath, file.RelativePath)), file);
            }
        }

        public void AddAsset(ContentCookInput input)
        {
            _ = this.RelativePath(input.SourceAbsolutePath.FullPath);
            if (this.assets.TryAdd(input.SourceAbsolutePath, input))
            {
                this.pending.Enqueue(input);
                CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Preparing)));

                // Files beside each other that cook to one asset, such as a pair made outside the editor, are both
                // refused: the identity has one source.
                foreach (var other in SourceCollisions.Find(project, input.SourceAbsolutePath))
                {
                    this.ReportCollision(input.AssetUri.VirtualPath, input.SourceAbsolutePath, other);
                }
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
                foreach (var input in analyzed.GeneratedInputs)
                {
                    this.generatedInputs[input.LogicalPath] = input;
                }

                foreach (var source in analyzed.Sources)
                {
                    this.ClaimOutputs(source);
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
                [.. this.published.Order()],
                [.. this.diagnostics])
            {
                ImportedSources = this.imported.ToImmutableDictionary(),
                ImportedReferences = [.. this.importedReferences],
                NativeReferences = this.nativeReferences.ToImmutableDictionary(),
                CookedDependencies = this.cookedDependencies.ToImmutableDictionary(),
                SceneDescriptors = this.scenes.ToImmutableDictionary(),
                NativeJobs = this.jobs.ToImmutableDictionary(),
                GeneratedInputs = [.. this.generatedInputs.Values],
                SourceFacts = this.sourceFacts.ToImmutableDictionary(),
                SourcesNeedingAnalysis = this.requiresAnalysis.ToImmutableHashSet(),
                ResourceDependencies = this.resourceDependencies.ToImmutableDictionary(),
            };
        }

        private async Task ApplySourceAsync(CookSourceFacts source, CancellationToken cancellationToken)
        {
            var input = source.Input;
            this.assets[input.SourceAbsolutePath] = input;
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
                var path = file.SourcePath;
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
                    [.. source.Files.Where(file => !string.Equals(file.RelativePath, input.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix, FileSystemPath.NameComparison))
                        .Select(static file => file.RelativePath).Order(StringComparer.Ordinal)])
                {
                    ContentFingerprint = FingerprintImportedContent(source.Files),
                };
            }

            var localOutputs = source.Outputs.Select(static output => output.VirtualPath).ToHashSet(StringComparer.Ordinal);
            var external = source.References.Where(reference => !localOutputs.Contains(reference.VirtualPath)).ToArray();
            var references = external.Where(static reference => !string.Equals(reference.Kind, "buffer", StringComparison.Ordinal))
                .Select(static reference => AssetPath.Parse(reference.VirtualPath))
                .Where(reference => AssetFileTypes.ContainerOf(reference) != input.AssetUri)
                .Distinct().ToArray();
            this.nativeReferences[input.AssetUri] = [.. references];
            var resolved = references.Select(this.ResolveReference).ToHashSet();
            var bufferDependencies = new HashSet<AssetPath>();
            foreach (var reference in external.Where(static reference => string.Equals(reference.Kind, "buffer", StringComparison.Ordinal)))
            {
                var owner = this.producers.GetValueOrDefault(reference.VirtualPath) ?? resolveProducer?.Invoke(reference.VirtualPath);
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

            this.dependencies[input.AssetUri] = [.. resolved.Order()];
        }

        // A reference is an asset's identity. It resolves to a cooked asset the cook publishes as it is (a library asset,
        // including the engine content pak's), or the project source that cooks into it.
        // A source's outputs are its own unless another source in this cook, or one the accepted facts record and the
        // project still holds, produces the same identity; then neither is cooked until one is renamed or removed.
        private void ClaimOutputs(CookSourceFacts source)
        {
            foreach (var output in source.Outputs)
            {
                var producer = this.producers.GetValueOrDefault(output.VirtualPath) ?? resolveProducer?.Invoke(output.VirtualPath);
                if (producer is null || producer.AssetUri == source.Input.AssetUri)
                {
                    this.producers[output.VirtualPath] = source.Input;
                    continue;
                }

                this.ReportCollision(output.VirtualPath, source.Input.SourceAbsolutePath, producer.SourceAbsolutePath);
            }
        }

        // Both sources of one identity are refused, each with the other named.
        private void ReportCollision(string virtualPath, FileSystemPath first, FileSystemPath second)
        {
            FileSystemPath[] colliding = [first, second];
            this.diagnostics.AddRange(colliding.Select(source => new DiagnosticRecord
            {
                OperationId = Guid.Empty,
                Domain = FailureDomain.AssetCook,
                Severity = DiagnosticSeverity.Error,
                Code = AssetCookDiagnosticCodes.IdentityCollision,
                AffectedPath = source.FullPath,
                AffectedVirtualPath = virtualPath,
                Message = $"'{first.Name}' and '{second.Name}' both produce '{virtualPath}'. Rename or remove one of them.",
            }));
        }

        private AssetPath ResolveReference(AssetPath uri)
        {
            if (preferCookedReference?.Invoke(uri) == true)
            {
                _ = this.published.Add(uri);
                return uri;
            }

            // A script with its source in the project cooks with the scene that uses it.
            if (CookInputResolver.IsAuthoringUri(project, uri) && CookInputResolver.TryResolveScript(project, uri, ContentCookInputRole.Dependency) is { } script)
            {
                this.AddAsset(script);
                return this.assets[script.SourceAbsolutePath].AssetUri;
            }

            // A product is named under its container, which cooks it; the cook checks that a model still produces it.
            if (resolveImported?.Invoke(uri) is { } owner)
            {
                _ = this.importedReferences.Add(uri);
                this.AddAsset(owner);
                return this.assets[owner.SourceAbsolutePath].AssetUri;
            }

            if (CookInputResolver.ResolveContainer(project, uri, ContentCookInputRole.Dependency) is { } container)
            {
                this.AddAsset(container);
                return this.assets[container.SourceAbsolutePath].AssetUri;
            }

            if (AssetFileTypes.Find(uri.Name) is { Category: AssetFileCategory.Cooked, CookedExtension: AssetFileTypes.Script or AssetFileTypes.InputAction or AssetFileTypes.InputMappingContext or AssetFileTypes.PhysicsScene })
            {
                _ = this.published.Add(uri);
                return uri;
            }

            if (!CookInputResolver.HasProjectSource(project, uri))
            {
                _ = this.published.Add(uri);
                return uri;
            }

            var input = CookInputResolver.Resolve(project, uri, ContentCookInputRole.Dependency);
            this.AddAsset(input);
            return this.assets[input.SourceAbsolutePath].AssetUri;
        }

        private string RelativePath(string path)
        {
            var relative = Path.GetRelativePath(project.ProjectRoot.FullPath, Path.GetFullPath(path)).Replace('\\', '/');
            return Path.IsPathRooted(relative) || relative is ".." || relative.StartsWith("../", StringComparison.Ordinal)
                ? throw new InvalidDataException($"Cook dependency '{path}' is outside the project. Retain it in the project before cooking.")
                : relative;
        }
    }
}
