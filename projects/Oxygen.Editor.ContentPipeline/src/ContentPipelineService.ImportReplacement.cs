// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Cooks reviewed replacement bytes privately before source and output commit together.</summary>
public sealed partial class ContentPipelineService
{
    private static CookDependencyGraph CreateReplacementGraph(ContentCookInput source, NativeSceneImportSettings settings, ImmutableArray<CookSnapshotInput> inputs, string settingsRelative, CookProvenance previous)
        => new(
            [source],
            inputs,
            ImmutableDictionary<AssetPath, ImmutableArray<AssetPath>>.Empty.Add(source.AssetUri, []),
            ImmutableDictionary<AssetPath, ImmutableArray<string>>.Empty.Add(source.AssetUri, [.. inputs.Select(static file => file.RelativePath)]),
            [],
            [])
        {
            ImportedSources = ImmutableDictionary<AssetPath, ImportedSourceDependencyState>.Empty.Add(
                source.AssetUri,
                new(
                    settings.SourceHash,
                    [.. inputs.Where(file => !string.Equals(file.RelativePath, settingsRelative, StringComparison.Ordinal)).Select(static file => file.RelativePath)])
                {
                    ContentFingerprint = CookDependencyDiscovery.FingerprintImportedContent(inputs),
                }),
            ImportedReferences = [.. previous.Products.Where(product => product.SourceUri == source.AssetUri).SelectMany(static product => product.Outputs).Select(static output => output.Asset.CookedAssetUri)],
        };

    private static CookSnapshotInput[] MapReplacementFiles(ContentCookOperation operation, ContentCookInput source, NativeSceneImportSettings settings, string incoming, ImportSourceBundle bundle)
    {
        var sourceRoot = FileSystemPath.From(settings.ResolveFile(operation.Project.ProjectRoot.FullPath, "."));
        return bundle.Files.Select(file =>
        {
            var primary = string.Equals(file.RelativePath, bundle.PrimaryRelativePath, FileSystemPath.NameComparison);
            var target = primary ? source.SourceAbsolutePath : source.SourceAbsolutePath.Parent!.Combine(Path.GetRelativePath(Path.GetDirectoryName(incoming)!, file.SourcePath.FullPath));
            var inside = target != sourceRoot && target.IsSameOrInside(sourceRoot);
            return !inside && !(file.Kind == CookSnapshotInputKind.Probe && file.Metadata?.IsDirectory == true && target == sourceRoot)
                ? throw new InvalidDataException("The incoming dependency layout cannot preserve the existing source location. Import it into a new destination.")
                : file with { AssetUri = primary ? source.AssetUri : null, RelativePath = target.GetPathBelow(operation.Project.ProjectRoot) };
        }).ToArray();
    }

    private static void RetainRolledBackReplacement(ContentCookScope scope, CookInputSnapshot snapshot)
    {
        var request = scope.ImportReplacement!;
        var replacement = request.Replacement!;
        var candidate = Path.Combine(snapshot.Operation.Project.ProjectRoot.FullPath, ".build", "cook", snapshot.Operation.OperationId.ToString("N"), "discarded-source", replacement.BundleName, replacement.Settings.PrimaryRelativePath);
        if (File.Exists(candidate))
        {
            var recovery = request with { ReplacementCandidatePath = candidate };
            scope.RetainReplacementCandidate?.Invoke(recovery);
            CookRunContext.Report(new()
            {
                RecoveryRequest = new(CookTargetKind.Asset, replacement.SourceUri) { Import = recovery, OriginContext = scope.Project },
            });
        }
    }

    private static void RequireCompleteReplacementBundle(
        ContentCookOperation operation,
        NativeSceneImportSettings settings,
        ImmutableArray<CookSnapshotInput> replacementFiles,
        CookDependencyGraph graph)
    {
        var root = FileSystemPath.From(settings.ResolveFile(operation.Project.ProjectRoot.FullPath, "."));
        var project = operation.Project.ProjectRoot;
        var incoming = replacementFiles.Select(file => project.Combine(file.RelativePath)).ToHashSet();
        var missing = graph.Files.FirstOrDefault(file => !file.IsAbsent && project.Combine(file.RelativePath) is var path
            && !incoming.Contains(path) && path != root && path.IsSameOrInside(root));
        if (missing is not null)
        {
            var owners = graph.FileDependencies.Where(pair => pair.Value.Contains(missing.RelativePath, StringComparer.Ordinal))
                .Select(static pair => pair.Key.VirtualPath);
            throw new CookInputDiscoveryException([new()
            {
                OperationId = operation.OperationId,
                Domain = Oxygen.Managed.Core.Diagnostics.FailureDomain.AssetCook,
                Severity = Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error,
                Code = "asset_cook.replacement_dependency_missing",
                AffectedPath = missing.SourcePath.FullPath,
                Message = $"{string.Join(", ", owners)} requires '{missing.RelativePath}', which the replacement would remove. Include it in the replacement or update the dependent source before replacing the model.",
            }
            ]);
        }
    }

    // The settings are rewritten for the incoming files and written beside the captured inputs.
    private static async Task<(NativeSceneImportSettings Settings, ImmutableArray<CookSnapshotInput> Inputs)> RewriteReplacementSettingsAsync(
        ContentCookOperation operation,
        ReplacementDiscovery discovery,
        CookInputSnapshot snapshot,
        HashSet<string> replacementPaths,
        CancellationToken cancellationToken)
    {
        var (source, original, settingsRelative) = (discovery.Source, discovery.Original, discovery.SettingsRelative);
        var inputs = snapshot.Inputs.Select(file => replacementPaths.Contains(file.RelativePath)
            ? file with { SourcePath = operation.Project.ProjectRoot.Combine(file.RelativePath) } : file).ToImmutableArray();
        var settings = original with
        {
            SourceHash = inputs.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash,
            Files = [.. inputs.Where(file => file.Kind == CookSnapshotInputKind.File && replacementPaths.Contains(file.RelativePath))
                .Select(file => Path.GetRelativePath(original.ResolveFile(operation.Project.ProjectRoot.FullPath, "."), file.SourcePath.FullPath).Replace('\\', '/')).Order(StringComparer.Ordinal)],
        };
        var bytes = settings.ToBytes();
        if (inputs.Any(file => operation.Project.ProjectRoot.Combine(file.RelativePath) == operation.Project.ProjectRoot.Combine(settingsRelative)))
        {
            throw new InvalidDataException("An incoming dependency conflicts with the existing import settings path.");
        }

        await File.WriteAllBytesAsync(Path.Combine(snapshot.InputRoot, settingsRelative), bytes, cancellationToken).ConfigureAwait(false);
        inputs = inputs.Add(new(null, operation.Project.ProjectRoot.Combine(settingsRelative), settingsRelative, Convert.ToHexString(SHA256.HashData(bytes)))
        {
            Metadata = CookSavedSourceReader.ReadMetadata(Path.Combine(snapshot.InputRoot, settingsRelative)),
        });
        return (settings, [.. inputs.OrderBy(static file => file.RelativePath, StringComparer.Ordinal)]);
    }

    private async Task<ContentCookResult> CookReplacementAsync(ContentCookOperation operation, SceneImportRequest request, Action<SceneImportRequest> retainRecovery, CancellationToken cancellationToken)
    {
        var replacement = request.Replacement!;
        var input = CookInputResolver.Resolve(operation.Project, replacement.SourceUri, ContentCookInputRole.Primary);
        var result = await this.CookCapturedScopesAsync(
            operation,
            () =>
            [
                this.CreateScope(operation.Project, [input], CookTargetKind.Asset) with
                {
                    ScopeUri = input.AssetUri, ImportReplacement = request, RetainReplacementCandidate = retainRecovery,
                },
            ],
            CookTargetKind.Asset,
            cancellationToken).ConfigureAwait(false);
        return result with { RetainedSourceUri = input.AssetUri };
    }

    private async Task<(CookInputSnapshot Snapshot, CookDependencyGraph Graph)> CaptureReplacementAsync(
        ContentCookOperation operation,
        ContentCookScope scope,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        NativeArtifactLease artifacts,
        CookProvenance previous,
        ImportedSourceIndex imports,
        CookedLibraryReadSet libraries,
        CancellationToken cancellationToken)
    {
        var request = scope.ImportReplacement!;
        var replacement = request.Replacement!;
        var original = replacement.Settings;
        await this.VerifyReplacementTargetAsync(operation, replacement, imports, cancellationToken).ConfigureAwait(false);
        var source = scope.Inputs.Single() with
        {
            MountName = original.MountPoint,
            OutputVirtualPath = null,
            OutputNamespaces = original.OutputPrefixes,
        };
        var incoming = request.ReplacementCandidatePath ?? request.SourcePath;
        if (!string.Equals(Path.GetExtension(incoming), Path.GetExtension(source.SourceAbsolutePath.Name), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Replacement must use the existing source format. Import a different format into a new destination.");
        }

        var settingsRelative = source.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix;
        var discovery = new ReplacementDiscovery(source, original, incoming, settingsRelative, resolveScopes, previous, imports, libraries);
        var (snapshot, graph, replacementFiles) = await this.CaptureIncomingAsync(operation, discovery, artifacts, cancellationToken).ConfigureAwait(false);
        var replacementPaths = replacementFiles.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
        var (settings, inputs) = await RewriteReplacementSettingsAsync(operation, discovery, snapshot, replacementPaths, cancellationToken).ConfigureAwait(false);
        var recovery = request with { ReplacementCandidatePath = Path.Combine(snapshot.InputRoot, source.SourceRelativePath) };
        scope.RetainReplacementCandidate?.Invoke(recovery);
        CookRunContext.Report(new(Message: "Replacement captured. Cooking before replacing the existing source.")
        {
            RecoveryRequest = new(CookTargetKind.Asset, source.AssetUri) { Import = recovery, OriginContext = operation.Project },
        });
        snapshot = snapshot with
        {
            Inputs = inputs,
            InputIdentity = CookInputSnapshotCapture.ComputeIdentity(snapshot.BuildFingerprint, inputs),
            SourceReplacement = new(replacement.BundleName, replacement.Before),
        };
        var replacementGraph = CreateReplacementGraph(
            source,
            settings,
            [.. inputs.Where(file => replacementPaths.Contains(file.RelativePath) || string.Equals(file.RelativePath, settingsRelative, StringComparison.Ordinal))],
            settingsRelative,
            previous);
        graph = graph with
        {
            Files = inputs,
            FileDependencies = graph.FileDependencies.SetItem(source.AssetUri, replacementGraph.FileDependencies[source.AssetUri]),
            ImportedSources = graph.ImportedSources.SetItem(source.AssetUri, replacementGraph.ImportedSources[source.AssetUri]),
        };
        return (snapshot, await this.AnalyzeReplacementAsync(operation, source, settings, snapshot, graph, artifacts, cancellationToken).ConfigureAwait(false));
    }

    private async Task VerifyReplacementTargetAsync(ContentCookOperation operation, SceneImportReplacement replacement, ImportedSourceIndex imports, CancellationToken cancellationToken)
    {
        var root = ImportSourceRetention.ResolveDestination(operation.Project, replacement.BundleName);
        using var owners = await cookDocuments.AcquireAsync(replacement.Settings.Files.Append(replacement.Settings.PrimaryRelativePath + NativeSceneImportSettings.SidecarSuffix).Select(path => FileSystemPath.From(Path.Combine(root, path))), cancellationToken).ConfigureAwait(false);
        var dirty = owners.Documents.Where(static document => document.IsDirty).ToArray();
        if (dirty.Length != 0)
        {
            throw new CookInputsNeedSaveException(dirty);
        }

        if (!replacement.Before.Matches(await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false)))
        {
            throw new IOException("The retained source changed after review. Review the replacement again.");
        }

        var settings = replacement.Settings;
        if (settings.OutputPrefixes.Any(prefix => imports.ResolveFolder(operation.Project, AssetPath.Parse(prefix.TrimEnd('/'))).Any(input => input.AssetUri != replacement.SourceUri)))
        {
            throw new InvalidDataException("The replacement destination overlaps another retained source. Resolve its ownership before replacing this model.");
        }

        var mount = operation.Project.AuthoringMounts.Single(mount => string.Equals(mount.Name, settings.MountPoint, StringComparison.Ordinal));
        var conflict = settings.OutputDirectories.Select(directory => Path.Combine(operation.Project.ProjectRoot.FullPath, mount.RelativePath, directory))
            .Where(Directory.Exists).SelectMany(static directory => Directory.EnumerateFiles(directory, "*.json", SearchOption.AllDirectories))
            .FirstOrDefault(static path => AssetFileTypes.ToCookedPath(Path.GetFileName(path)) is { } cooked && AssetFileTypes.IsProductOf(cooked, AssetFileCategory.Model));
        if (conflict is not null)
        {
            throw new InvalidDataException($"The replacement destination contains authored asset '{conflict}'. Keep it in a separate destination before replacing the model.");
        }
    }

    // Captures the incoming files and discovers what the replacement depends on.
    private async Task<(CookInputSnapshot Snapshot, CookDependencyGraph Graph, ImmutableArray<CookSnapshotInput> ReplacementFiles)> CaptureIncomingAsync(
        ContentCookOperation operation,
        ReplacementDiscovery discovery,
        NativeArtifactLease artifacts,
        CancellationToken cancellationToken)
    {
        var (source, original) = (discovery.Source, discovery.Original);
        CookDependencyGraph? graph = null;
        ImmutableArray<CookSnapshotInput> replacementFiles = [];
        var capture = await new CookInputSnapshotCapture(cookDocuments, this.cookCoordinator).CaptureAsync(
            operation,
            async token =>
            {
                var discovered = await this.DiscoverChangedImportedSourceAsync(operation, source with { SourceAbsolutePath = FileSystemPath.From(discovery.Incoming) }, original, artifacts, token).ConfigureAwait(false);
                replacementFiles = [.. MapReplacementFiles(operation, source, original, discovery.Incoming, discovered)];
                var incomingSettings = original with
                {
                    SourceHash = replacementFiles.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash,
                };
                var prepared = CreateReplacementGraph(source, incomingSettings, replacementFiles, discovery.SettingsRelative, discovery.Previous);
                var scopes = discovery.ResolveScopes();
                graph = await this.CreateDependencyDiscovery(operation, artifacts, discovery.Previous, discovery.Imports, discovery.Libraries)
                    .DiscoverAsync(operation.Project, scopes.SelectMany(static value => value.Inputs).ToArray(), prepared, token).ConfigureAwait(false);
                graph = graph with { ImportedReferences = [.. graph.ImportedReferences.Union(scopes.SelectMany(static value => value.RequiredImportedOutputs))] };
                RequireCompleteReplacementBundle(operation, original, replacementFiles, graph);
                return HasError(graph.Diagnostics) ? throw new CookInputDiscoveryException(graph.Diagnostics) : graph.Files;
            },
            artifacts.Fingerprint,
            cancellationToken).ConfigureAwait(false);
        var snapshot = capture.Snapshot ?? (capture.NeedsSave.IsEmpty
            ? throw new IOException("The incoming source changed. Review the replacement again.")
            : throw new CookInputsNeedSaveException(capture.NeedsSave));
        return (snapshot, graph!, replacementFiles);
    }

    // Native analysis of the captured replacement supplies its outputs and references.
    private async Task<CookDependencyGraph> AnalyzeReplacementAsync(
        ContentCookOperation operation,
        ContentCookInput source,
        NativeSceneImportSettings settings,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        NativeArtifactLease artifacts,
        CancellationToken cancellationToken)
    {
        var job = this.manifestBuilder.BuildJob(source, [], settings);
        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot.FullPath,
            Path.Combine(operation.Project.ProjectRoot.FullPath, ".build", "cook", operation.OperationId.ToString("N")),
            [job])
        {
            Artifacts = artifacts,
            CapturedInputs = snapshot.CreateNativeInputs(),
        };
        var analysis = await this.engineContentPipelineApi.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        if (!analysis.Complete)
        {
            var issues = analysis.Jobs.SelectMany(static result => result.Diagnostics).ToArray();
            throw issues.Length != 0 ? new CookInputDiscoveryException(issues)
                : new InvalidDataException("Native analysis of the captured replacement did not complete.");
        }

        var facts = analysis.Jobs.Single();
        var dependencies = snapshot.Inputs.Where(file => graph.FileDependencies[source.AssetUri].Contains(file.RelativePath, StringComparer.Ordinal));
        return graph with
        {
            NativeJobs = graph.NativeJobs.SetItem(source.AssetUri, job),
            SourceFacts = graph.SourceFacts.SetItem(source.AssetUri, new(source, facts.Outputs, facts.References, [.. dependencies]) { Job = job }),
        };
    }

    /// <summary>What discovering a replacement needs, beside the operation and the artifacts.</summary>
    /// <param name="Source">The retained source, in its own mount and output namespaces.</param>
    /// <param name="Original">The import settings the replacement starts from.</param>
    /// <param name="Incoming">The incoming source file.</param>
    /// <param name="SettingsRelative">The import settings' project-relative path.</param>
    /// <param name="ResolveScopes">Resolves the cook scopes at discovery time.</param>
    /// <param name="Previous">The previous cook provenance.</param>
    /// <param name="Imports">The retained imported sources.</param>
    /// <param name="Libraries">The cooked libraries read by the cook.</param>
    private sealed record ReplacementDiscovery(
        ContentCookInput Source,
        NativeSceneImportSettings Original,
        string Incoming,
        string SettingsRelative,
        Func<IReadOnlyList<ContentCookScope>> ResolveScopes,
        CookProvenance Previous,
        ImportedSourceIndex Imports,
        CookedLibraryReadSet Libraries);
}
