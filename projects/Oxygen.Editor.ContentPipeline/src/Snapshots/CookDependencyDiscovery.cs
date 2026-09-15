// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Discovers saved scene, scalar material and static geometry dependencies without generating output.</summary>
/// <param name="documents">Document owners coordinating saved-source reads.</param>
/// <param name="allowUnsavedDocuments">Allows read-only inspection of saved inputs while their documents contain newer edits.</param>
/// <param name="importedSources">Previously published native dependency layouts for exact source revisions.</param>
/// <param name="discoverImported">Optional native rediscovery used only by an owned cook operation.</param>
/// <param name="resolveImported">Resolves native output references to their retained source owner.</param>
/// <param name="preferCookedReference">Tests whether a library overrides the corresponding project source.</param>
/// <param name="expandCookedReferences">Expands verified library dependencies before resolving authored owners.</param>
public sealed class CookDependencyDiscovery(
    ICookDocumentRegistry documents,
    bool allowUnsavedDocuments = false,
    IReadOnlyDictionary<Uri, ImportedSourceDependencyState>? importedSources = null,
    Func<ContentCookInput, CancellationToken, Task<DiscoveredSceneSource>>? discoverImported = null,
    Func<Uri, ContentCookInput?>? resolveImported = null,
    Func<Uri, bool>? preferCookedReference = null,
    Func<ContentCookInput, IReadOnlyList<Uri>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences = null)
{
    /// <summary>Reads the requested authored closure and hashes the exact bytes used to discover each dependency.</summary>
    /// <param name="project">The project whose authoring mounts resolve asset identities.</param>
    /// <param name="roots">Requested primary inputs.</param>
    /// <param name="cancellationToken">Cancels discovery.</param>
    /// <returns>The closure for coherent capture, planning and published-reference validation.</returns>
    public async Task<CookDependencyGraph> DiscoverAsync(ProjectContext project, IReadOnlyList<ContentCookInput> roots, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(roots);
        var discovery = new Discovery(project, documents, allowUnsavedDocuments, importedSources, discoverImported, resolveImported, preferCookedReference, expandCookedReferences);
        foreach (var input in roots)
        {
            discovery.AddAsset(input);
        }

        return await discovery.RunAsync(cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Fingerprints a retained model independently of the native producer version.</summary>
    /// <param name="inputs">The complete source, dependency and settings inputs.</param>
    /// <returns>The source content fingerprint used by automatic-cook policy.</returns>
    internal static string FingerprintImportedContent(IEnumerable<CookSnapshotInput> inputs)
        => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(inputs.OrderBy(static file => file.RelativePath, StringComparer.Ordinal)
            .Select(static file => new { file.RelativePath, file.DiscoveryHash }))));

    private sealed class Discovery(ProjectContext project, ICookDocumentRegistry documents, bool allowUnsavedDocuments,
        IReadOnlyDictionary<Uri, ImportedSourceDependencyState>? priorImports,
        Func<ContentCookInput, CancellationToken, Task<DiscoveredSceneSource>>? discoverImported,
        Func<Uri, ContentCookInput?>? resolveImported,
        Func<Uri, bool>? preferCookedReference,
        Func<ContentCookInput, IReadOnlyList<Uri>, CancellationToken, Task<CookReferenceExpansion>>? expandCookedReferences)
    {
        private readonly Dictionary<string, ContentCookInput> assets = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<string, CookSnapshotInput> files = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<string, byte[]> discoveredBytes = [with(StringComparer.OrdinalIgnoreCase)];
        private readonly Dictionary<Uri, ImmutableArray<Uri>> dependencies = [];
        private readonly Dictionary<Uri, ImmutableArray<Uri>> nativeReferences = [];
        private readonly Dictionary<Uri, ImmutableArray<CookedDependencySnapshot>> cookedDependencies = [];
        private readonly Dictionary<Uri, HashSet<string>> fileDependencies = [];
        private readonly HashSet<Uri> builtins = [];
        private readonly HashSet<Uri> published = [];
        private readonly HashSet<Uri> importedReferences = [];
        private readonly HashSet<Uri> importsNeedingDiscovery = [];
        private readonly Queue<ContentCookInput> pending = new();
        private readonly List<DiagnosticRecord> diagnostics = [];
        private readonly Dictionary<Uri, ImportedSourceDependencyState> imported = [];
        private Uri currentAsset = null!;

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
            while (this.pending.TryDequeue(out var input))
            {
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    await this.ReadAssetAsync(input, cancellationToken).ConfigureAwait(false);
                }
                catch (CookInputDiscoveryException failure)
                {
                    this.diagnostics.AddRange(failure.Diagnostics.Select(issue => issue with { AffectedVirtualPath = input.AssetUri.AbsolutePath }));
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or JsonException or KeyNotFoundException or InvalidOperationException or FormatException)
                {
                    this.diagnostics.Add(new()
                    {
                        OperationId = Guid.Empty,
                        Domain = FailureDomain.ContentPipeline,
                        Severity = DiagnosticSeverity.Error,
                        Code = ex is FileNotFoundException ? AssetImportDiagnosticCodes.SourceMissing : ContentPipelineDiagnosticCodes.ManifestGenerationFailed,
                        Message = ex.Message,
                        TechnicalMessage = ex.ToString(),
                        ExceptionType = ex.GetType().FullName,
                        AffectedPath = input.SourceAbsolutePath,
                        AffectedVirtualPath = input.AssetUri.AbsolutePath,
                    });
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
                ImportsNeedingDiscovery = this.importsNeedingDiscovery.ToImmutableHashSet(),
                NativeReferences = this.nativeReferences.ToImmutableDictionary(),
                CookedDependencies = this.cookedDependencies.ToImmutableDictionary(),
            };
        }

        private static Uri[] ReadMaterial(ContentCookInput input, byte[] bytes)
        {
            var material = MaterialSourceReader.Read(bytes);
            return material.PbrMetallicRoughness.BaseColorTexture is not null
                || material.PbrMetallicRoughness.MetallicRoughnessTexture is not null
                || material.NormalTexture is not null || material.OcclusionTexture is not null
                ? throw new InvalidDataException($"Material '{input.AssetUri}' contains textures. Material cooking supports scalar properties only.")
                : [];
        }

        private static ContentCookInput WithImportedOutputs(ContentCookInput input, NativeSceneImportSettings settings) => input with
        {
            MountName = settings.MountPoint,
            OutputVirtualPath = settings.SchemaVersion == 2 ? settings.OutputPrefixes[0] : null,
            OutputNamespaces = settings.OutputPrefixes,
        };

        private async Task ReadAssetAsync(ContentCookInput input, CancellationToken cancellationToken)
        {
            this.currentAsset = input.AssetUri;
            this.fileDependencies[input.AssetUri] = [with(StringComparer.Ordinal)];
            if (input.Kind == ContentCookAssetKind.ForeignSource)
            {
                await this.ReadImportedSourceAsync(input, cancellationToken).ConfigureAwait(false);
                this.dependencies[input.AssetUri] = [];
                return;
            }

            var bytes = await this.ReadFileAsync(input.AssetUri, input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            await this.ReadSettingsAsync(input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            IReadOnlyList<Uri> references = input.Kind switch
            {
                ContentCookAssetKind.Scene => await this.ReadSceneAsync(bytes, cancellationToken).ConfigureAwait(false),
                ContentCookAssetKind.Geometry => await this.ReadGeometryAsync(input, bytes, cancellationToken).ConfigureAwait(false),
                ContentCookAssetKind.Material => ReadMaterial(input, bytes),
                _ => throw new InvalidDataException($"Unsupported cook input '{input.AssetUri}'."),
            };
            this.nativeReferences[input.AssetUri] = [.. references.Select(static uri => uri.AbsolutePath.EndsWith(".json", StringComparison.OrdinalIgnoreCase) ? new Uri(uri.AbsoluteUri[..^5]) : uri).Distinct()];
            var resolvedDependencies = references.Select(this.ResolveReference).ToHashSet();
            if (expandCookedReferences is not null)
            {
                var expansion = await expandCookedReferences(input, references, cancellationToken).ConfigureAwait(false);
                this.cookedDependencies[input.AssetUri] = expansion.Libraries;
                this.diagnostics.AddRange(expansion.Diagnostics);
                this.importedReferences.UnionWith(expansion.ImportedOutputs);
                foreach (var dependency in expansion.ProjectInputs)
                {
                    this.AddAsset(dependency);
                    _ = resolvedDependencies.Add(dependency.AssetUri);
                }
            }

            this.dependencies[input.AssetUri] = [.. resolvedDependencies.OrderBy(static uri => uri.AbsoluteUri, StringComparer.Ordinal)];
        }

        private async Task ReadImportedSourceAsync(ContentCookInput input, CancellationToken cancellationToken)
        {
            var settingsBytes = await this.ReadFileAsync(assetUri: null, input.SourceAbsolutePath + NativeSceneImportSettings.SidecarSuffix, cancellationToken).ConfigureAwait(false);
            var settings = NativeSceneImportSettings.Parse(settingsBytes);
            if (!string.Equals(settings.ResolveFile(project.ProjectRoot, settings.PrimaryRelativePath), Path.GetFullPath(input.SourceAbsolutePath), StringComparison.OrdinalIgnoreCase)
                || !project.AuthoringMounts.Any(mount => string.Equals(mount.Name, settings.MountPoint, StringComparison.OrdinalIgnoreCase)))
            {
                throw new InvalidDataException("Import settings do not match their retained source or output mount.");
            }

            var primaryHash = await this.ReadHashAsync(input.AssetUri, input.SourceAbsolutePath, cancellationToken).ConfigureAwait(false);
            IEnumerable<string> paths;
            if (priorImports?.TryGetValue(input.AssetUri, out var prior) == true && string.Equals(prior.PrimaryHash, primaryHash, StringComparison.OrdinalIgnoreCase))
            {
                paths = prior.Files.Select(relative => Path.GetFullPath(Path.Combine(project.ProjectRoot, relative)));
            }
            else if (string.Equals(settings.SourceHash, primaryHash, StringComparison.OrdinalIgnoreCase))
            {
                paths = settings.Files.Select(relative => settings.ResolveFile(project.ProjectRoot, relative));
            }
            else if (discoverImported is not null)
            {
                var discovered = await discoverImported(input, cancellationToken).ConfigureAwait(false);
                foreach (var file in discovered.Bundle.Files)
                {
                    var relative = this.RelativePath(file.SourcePath);
                    this.files[file.SourcePath] = file with { RelativePath = relative };
                    _ = this.fileDependencies[input.AssetUri].Add(relative);
                }

                primaryHash = discovered.Bundle.Files.Single(file => string.Equals(file.SourcePath, input.SourceAbsolutePath, StringComparison.OrdinalIgnoreCase)).DiscoveryHash;
                paths = discovered.Bundle.Files.Select(static file => file.SourcePath);
            }
            else
            {
                if (!allowUnsavedDocuments)
                {
                    throw new InvalidDataException("The imported source changed. Cook or reimport it to update its saved dependency information.");
                }

                _ = this.importsNeedingDiscovery.Add(input.AssetUri);
                return;
            }

            var knownPaths = paths.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            foreach (var path in knownPaths)
            {
                var relative = this.RelativePath(path);
                _ = this.fileDependencies[input.AssetUri].Add(relative);
                if (!this.files.ContainsKey(path))
                {
                    _ = await this.ReadHashAsync(assetUri: null, path, cancellationToken).ConfigureAwait(false);
                }
            }

            this.assets[Path.GetFullPath(input.SourceAbsolutePath)] = WithImportedOutputs(input, settings);
            this.imported[input.AssetUri] = new(primaryHash, knownPaths.Select(this.RelativePath).Order(StringComparer.Ordinal).ToImmutableArray())
            {
                ContentFingerprint = FingerprintImportedContent(knownPaths.Append(input.SourceAbsolutePath + NativeSceneImportSettings.SidecarSuffix).Select(path => this.files[Path.GetFullPath(path)])),
            };
        }

        private async Task<string> ReadHashAsync(Uri? assetUri, string path, CancellationToken cancellationToken)
        {
            path = Path.GetFullPath(path);
            var relative = this.RelativePath(path);
            _ = this.fileDependencies[this.currentAsset].Add(relative);
            var hash = await CookSavedSourceReader.HashAsync(documents, path, cancellationToken, allowUnsavedDocuments).ConfigureAwait(false);
            this.files[path] = new(assetUri, path, relative, hash);
            return hash;
        }

        private async Task<byte[]> ReadFileAsync(Uri? assetUri, string path, CancellationToken cancellationToken)
        {
            path = Path.GetFullPath(path);
            _ = this.fileDependencies[this.currentAsset].Add(this.RelativePath(path));
            if (this.discoveredBytes.TryGetValue(path, out var captured))
            {
                return captured;
            }

            var relative = this.RelativePath(path);
            var bytes = await CookSavedSourceReader.ReadAsync(documents, path, cancellationToken, allowUnsavedDocuments).ConfigureAwait(false);
            this.discoveredBytes.Add(path, bytes);
            _ = this.files.TryAdd(path, new(assetUri, path, relative, Convert.ToHexString(SHA256.HashData(bytes))));
            return bytes;
        }

        private async Task ReadSettingsAsync(string sourcePath, CancellationToken cancellationToken)
        {
            var settings = sourcePath + ".import.json";
            _ = this.fileDependencies[this.currentAsset].Add(this.RelativePath(settings));
            if (CookSavedSourceReader.Exists(settings))
            {
                _ = await this.ReadFileAsync(assetUri: null, settings, cancellationToken).ConfigureAwait(false);
            }
            else
            {
                _ = this.files.TryAdd(Path.GetFullPath(settings), new(AssetUri: null, Path.GetFullPath(settings), this.RelativePath(settings), DiscoveryHash: string.Empty, IsAbsent: true));
            }
        }

        private async Task<Uri[]> ReadSceneAsync(byte[] bytes, CancellationToken cancellationToken)
        {
            var info = new ProjectInfo(project.ProjectId, project.Name, project.Category, project.ProjectRoot, project.Thumbnail)
            {
                AuthoringMounts = [.. project.AuthoringMounts],
                LocalFolderMounts = [.. project.LocalFolderMounts],
            };
            var owner = new Project(info) { Name = project.Name };
            var stream = new MemoryStream(bytes, writable: false);
            await using var lifetime = stream.ConfigureAwait(false);
            var scene = await new SceneSerializer(owner).DeserializeAsync(stream).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            return
            [
                .. scene.AllNodes.SelectMany(static node => node.Components.OfType<GeometryComponent>())
                .SelectMany(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>().Select(static slot => slot.Material.Uri).Prepend(geometry.Geometry?.Uri))
                .OfType<Uri>().Where(static uri => !string.Equals(uri.AbsolutePath, "/__uninitialized__", StringComparison.Ordinal)),
            ];
        }

        private async Task<Uri[]> ReadGeometryAsync(ContentCookInput input, byte[] bytes, CancellationToken cancellationToken)
        {
            using var descriptor = JsonDocument.Parse(bytes);
            var root = descriptor.RootElement;
            var localBuffers = new HashSet<string>(StringComparer.Ordinal);
            if (root.TryGetProperty("buffers", out var buffers))
            {
                foreach (var buffer in buffers.EnumerateArray())
                {
                    _ = localBuffers.Add(buffer.GetProperty("virtual_path").GetString()!);
                    var relative = buffer.GetProperty("uri").GetString() ?? throw new InvalidDataException($"Geometry '{input.AssetUri}' has an empty buffer URI.");
                    if (!Path.IsPathRooted(relative) && Uri.TryCreate(relative, UriKind.Absolute, out _))
                    {
                        throw new InvalidDataException($"Geometry '{input.AssetUri}' buffer '{relative}' is not a local file reference.");
                    }

                    var path = Path.GetFullPath(Path.IsPathRooted(relative) ? relative : Path.Combine(Path.GetDirectoryName(input.SourceAbsolutePath)!, relative));
                    _ = await this.ReadFileAsync(assetUri: null, path, cancellationToken).ConfigureAwait(false);
                }
            }

            var references = new List<Uri>();
            foreach (var lod in root.GetProperty("lods").EnumerateArray())
            {
                if (string.Equals(lod.GetProperty("mesh_type").GetString(), "skinned", StringComparison.Ordinal))
                {
                    throw new InvalidDataException($"Geometry '{input.AssetUri}' is skinned. Geometry cooking supports static meshes only.");
                }

                if (lod.TryGetProperty("buffers", out var meshBuffers))
                {
                    foreach (var buffer in meshBuffers.EnumerateObject())
                    {
                        var virtualPath = buffer.Value.GetString()!;
                        if (!localBuffers.Contains(virtualPath))
                        {
                            _ = this.published.Add(new Uri($"{AssetUris.Scheme}://{virtualPath}"));
                        }
                    }
                }

                foreach (var submesh in lod.GetProperty("submeshes").EnumerateArray())
                {
                    references.Add(new Uri($"{AssetUris.Scheme}://{submesh.GetProperty("material_ref").GetString()}"));
                }
            }

            return [.. references];
        }

        private Uri ResolveReference(Uri uri)
        {
            if (!string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Cook dependency '{uri}' must be an asset identity.");
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
