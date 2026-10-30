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
    private static CookDependencyGraph CreateReplacementGraph(ContentCookInput source, string primaryHash, ImmutableArray<CookSnapshotInput> inputs, string recordRelative, CookProvenance previous)
        => new(
            [source],
            inputs,
            ImmutableDictionary<AssetPath, ImmutableArray<AssetPath>>.Empty.Add(source.AssetUri, []),
            ImmutableDictionary<AssetPath, ImmutableArray<string>>.Empty.Add(source.AssetUri, [.. inputs.Select(static file => file.CookPath)]),
            [],
            [])
        {
            ImportedSources = ImmutableDictionary<AssetPath, ImportedSourceDependencyState>.Empty.Add(
                source.AssetUri,
                new(
                    primaryHash,
                    [.. inputs.Where(file => !string.Equals(file.CookPath, recordRelative, StringComparison.Ordinal)).Select(static file => file.CookPath)])
                {
                    ContentFingerprint = CookDependencyDiscovery.FingerprintImportedContent(inputs),
                }),
            ImportedReferences = [.. previous.Products.Where(product => product.SourceUri == source.AssetUri).SelectMany(static product => product.Outputs).Select(static output => output.Asset.CookedAssetUri)],
        };

    private static CookSnapshotInput[] MapReplacementFiles(ContentCookOperation operation, ContentCookInput source, SceneImportReplacement replacement, string incoming, ImportSourceBundle bundle)
    {
        var sourceRoot = CookPaths.FileOf(operation.Project, replacement.BundleRoot);
        return [.. bundle.Files.Select(file =>
        {
            var primary = string.Equals(file.CookPath, bundle.PrimaryRelativePath, FileSystemPath.NameComparison);
            var target = primary ? source.SourceAbsolutePath : source.SourceAbsolutePath.Parent!.Combine(Path.GetRelativePath(Path.GetDirectoryName(incoming)!, file.SourcePath.FullPath));
            var inside = target != sourceRoot && target.IsSameOrInside(sourceRoot);
            return !inside && !(file.Kind == CookSnapshotInputKind.Probe && file.Metadata?.IsDirectory == true && target == sourceRoot)
                ? throw new InvalidDataException("The incoming dependency layout cannot preserve the existing source location. Import it into a new destination.")
                : file with { AssetUri = primary ? source.AssetUri : null, CookPath = CookPaths.Of(operation.Project, target) };
        })];
    }

    private static void RetainRolledBackReplacement(ContentCookScope scope, CookInputSnapshot snapshot)
    {
        var request = scope.ImportReplacement!;
        var replacement = request.Replacement!;
        var candidate = Path.Combine(CookPublicationPaths.Operation(snapshot.Operation.Project.ProjectRoot.FullPath, snapshot.Operation.OperationId), "discarded-source", Path.GetFileName(replacement.BundleRoot), replacement.PrimaryRelativePath);
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
        SceneImportReplacement replacement,
        ImmutableArray<CookSnapshotInput> replacementFiles,
        CookDependencyGraph graph)
    {
        var project = operation.Project;
        var root = CookPaths.FileOf(project, replacement.BundleRoot);
        var incoming = replacementFiles.Select(file => CookPaths.FileOf(project, file.CookPath)).ToHashSet();
        var missing = graph.Files.FirstOrDefault(file => !file.IsAbsent && CookPaths.FileOf(project, file.CookPath) is var path
            && !incoming.Contains(path) && path != root && path.IsSameOrInside(root));
        if (missing is not null)
        {
            var owners = graph.FileDependencies.Where(pair => pair.Value.Contains(missing.CookPath, StringComparer.Ordinal))
                .Select(static pair => pair.Key.VirtualPath);
            throw new CookInputDiscoveryException([new()
            {
                OperationId = operation.OperationId,
                Domain = Oxygen.Managed.Core.Diagnostics.FailureDomain.AssetCook,
                Severity = Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error,
                Code = "asset_cook.replacement_dependency_missing",
                AffectedPath = missing.SourcePath.FullPath,
                Message = $"{string.Join(", ", owners)} requires '{missing.CookPath}', which the replacement would remove. Include it in the replacement or update the dependent source before replacing the model.",
            }
            ]);
        }
    }

    // The replacement keeps the model's record, captured beside the incoming files so the cook commits what it establishes.
    private static async Task<ImmutableArray<CookSnapshotInput>> CaptureReplacementRecordAsync(
        ContentCookOperation operation,
        ReplacementDiscovery discovery,
        CookInputSnapshot snapshot,
        HashSet<string> replacementPaths,
        CancellationToken cancellationToken)
    {
        var project = operation.Project;
        var inputs = snapshot.Inputs.Select(file => replacementPaths.Contains(file.CookPath)
            ? file with { SourcePath = CookPaths.FileOf(project, file.CookPath) } : file).ToImmutableArray();
        if (inputs.Any(file => CookPaths.FileOf(project, file.CookPath) == CookPaths.FileOf(project, discovery.RecordRelative)))
        {
            throw new InvalidDataException("An incoming dependency conflicts with the model's import record.");
        }

        var record = discovery.Replacement.Record;
        var bytes = await File.ReadAllBytesAsync(record.RecordPath, cancellationToken).ConfigureAwait(false);
        _ = Models.ModelImportSettings.Parse(record.RecordPath, bytes);
        var captured = Path.Combine(snapshot.InputRoot, CookPaths.StoragePathOf(discovery.RecordRelative));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(captured)!);
        await File.WriteAllBytesAsync(captured, bytes, cancellationToken).ConfigureAwait(false);
        inputs = inputs.Add(new(AssetUri: null, CookPaths.FileOf(project, discovery.RecordRelative), discovery.RecordRelative, Convert.ToHexString(SHA256.HashData(bytes)))
        {
            Metadata = CookSavedSourceReader.ReadMetadata(captured),
        });
        return [.. inputs.OrderBy(static file => file.CookPath, StringComparer.Ordinal)];
    }

    private static async Task VerifyReplacementTargetAsync(ContentCookOperation operation, SceneImportReplacement replacement, CancellationToken cancellationToken)
    {
        var root = CookPaths.FileOf(operation.Project, replacement.BundleRoot).FullPath;
        if (!replacement.Before.Matches(await CookRootImage.CaptureAsync(root, copyTo: null, cancellationToken).ConfigureAwait(false)))
        {
            throw new IOException("The model to replace changed since its replacement was chosen. Choose Replace again.");
        }
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
                    ScopeUri = input.AssetUri, ImportReplacement = request, RetainReplacementCandidate = retainRecovery, Run = request.Run,
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
        CookedLibraryReadSet libraries,
        CancellationToken cancellationToken)
    {
        var request = scope.ImportReplacement!;
        var replacement = request.Replacement!;
        await VerifyReplacementTargetAsync(operation, replacement, cancellationToken).ConfigureAwait(false);
        var source = scope.Inputs.Single();
        var incoming = request.ReplacementCandidatePath ?? request.SourcePath;
        if (!string.Equals(Path.GetExtension(incoming), Path.GetExtension(source.SourceAbsolutePath.Name), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Replacement must use the existing source format. Import a different format into a new destination.");
        }

        var recordRelative = ModelRecordLookup.RelativePath(operation.Project, replacement.Record);
        var discovery = new ReplacementDiscovery(source, replacement, incoming, recordRelative, resolveScopes, previous, libraries);
        var (snapshot, graph, replacementFiles) = await this.CaptureIncomingAsync(operation, discovery, artifacts, cancellationToken).ConfigureAwait(false);
        var replacementPaths = replacementFiles.Select(static file => file.CookPath).ToHashSet(StringComparer.Ordinal);
        var inputs = await CaptureReplacementRecordAsync(operation, discovery, snapshot, replacementPaths, cancellationToken).ConfigureAwait(false);
        var recovery = request with { ReplacementCandidatePath = Path.Combine(snapshot.InputRoot, CookPaths.StoragePathOf(source.SourceCookPath)) };
        scope.RetainReplacementCandidate?.Invoke(recovery);
        CookRunContext.Report(new(Message: "Replacement captured. Cooking before replacing the existing source.")
        {
            RecoveryRequest = new(CookTargetKind.Asset, source.AssetUri) { Import = recovery, OriginContext = operation.Project },
        });
        snapshot = snapshot with
        {
            Inputs = inputs,
            InputIdentity = CookInputSnapshotCapture.ComputeIdentity(inputs),
            SourceReplacement = new(replacement.BundleRoot, replacement.Before),
        };
        var replacementGraph = CreateReplacementGraph(
            source,
            inputs.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash,
            [.. inputs.Where(file => replacementPaths.Contains(file.CookPath) || string.Equals(file.CookPath, recordRelative, StringComparison.Ordinal))],
            recordRelative,
            previous);
        graph = graph with
        {
            Files = inputs,
            FileDependencies = graph.FileDependencies.SetItem(source.AssetUri, replacementGraph.FileDependencies[source.AssetUri]),
            ImportedSources = graph.ImportedSources.SetItem(source.AssetUri, replacementGraph.ImportedSources[source.AssetUri]),
        };
        return (snapshot, await this.AnalyzeReplacementAsync(operation, source, recordRelative, snapshot, graph, artifacts, cancellationToken).ConfigureAwait(false));
    }

    // Captures the incoming files and discovers what the replacement depends on.
    private async Task<(CookInputSnapshot Snapshot, CookDependencyGraph Graph, ImmutableArray<CookSnapshotInput> ReplacementFiles)> CaptureIncomingAsync(
        ContentCookOperation operation,
        ReplacementDiscovery discovery,
        NativeArtifactLease artifacts,
        CancellationToken cancellationToken)
    {
        var (source, replacement) = (discovery.Source, discovery.Replacement);
        CookDependencyGraph? graph = null;
        ImmutableArray<CookSnapshotInput> replacementFiles = [];
        var snapshot = await new CookInputSnapshotCapture(this.cookCoordinator).CaptureAsync(
            operation,
            async token =>
            {
                var discovered = await this.DiscoverChangedImportedSourceAsync(operation, source with { SourceAbsolutePath = FileSystemPath.From(discovery.Incoming) }, artifacts, token).ConfigureAwait(false);
                replacementFiles = [.. MapReplacementFiles(operation, source, replacement, discovery.Incoming, discovered)];
                var primaryHash = replacementFiles.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash;
                var prepared = CreateReplacementGraph(source, primaryHash, replacementFiles, discovery.RecordRelative, discovery.Previous);
                var scopes = discovery.ResolveScopes();
                graph = await this.CreateDependencyDiscovery(operation, artifacts, discovery.Previous, discovery.Libraries)
                    .DiscoverAsync(operation.Project, [.. scopes.SelectMany(static value => value.Inputs)], prepared, token).ConfigureAwait(false);
                graph = graph with { ImportedReferences = [.. graph.ImportedReferences.Union(scopes.SelectMany(static value => value.RequiredImportedOutputs))] };
                RequireCompleteReplacementBundle(operation, replacement, replacementFiles, graph);
                return HasError(graph.Diagnostics) ? throw new CookInputDiscoveryException(graph.Diagnostics) : graph.Files;
            },
            cancellationToken).ConfigureAwait(false);
        return (snapshot, graph!, replacementFiles);
    }

    // Native analysis of the captured replacement supplies its outputs and references.
    private async Task<CookDependencyGraph> AnalyzeReplacementAsync(
        ContentCookOperation operation,
        ContentCookInput source,
        string recordRelative,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        NativeArtifactLease artifacts,
        CancellationToken cancellationToken)
    {
        var job = this.manifestBuilder.BuildJob(source, [], recordRelative);
        var execution = new ContentSourceAnalysisExecution(
            operation.OperationId,
            operation.Project.ProjectRoot.FullPath,
            CookPublicationPaths.Operation(operation.Project.ProjectRoot.FullPath, operation.OperationId),
            [job])
        {
            Artifacts = artifacts,
            CapturedInputs = snapshot.CreateNativeInputs(),
            Mounts = ContentImportManifestBuilder.CreateMounts(operation.Project),
            MountsFile = ContentImportMountsFile.Of(operation.Project),
        };
        var analysis = await this.engineContentPipelineApi.AnalyzeSourcesAsync(execution, cancellationToken).ConfigureAwait(false);
        if (!analysis.Complete)
        {
            var issues = analysis.Jobs.SelectMany(static result => result.Diagnostics).ToArray();
            throw issues.Length != 0 ? new CookInputDiscoveryException(issues)
                : new InvalidDataException("Native analysis of the captured replacement did not complete.");
        }

        var facts = analysis.Jobs.Single();
        var dependencies = snapshot.Inputs.Where(file => graph.FileDependencies[source.AssetUri].Contains(file.CookPath, StringComparer.Ordinal));
        return graph with
        {
            NativeJobs = graph.NativeJobs.SetItem(source.AssetUri, job),
            SourceFacts = graph.SourceFacts.SetItem(source.AssetUri, new(source, facts.Outputs, facts.References, [.. dependencies]) { Job = job }),
        };
    }

    /// <summary>What discovering a replacement needs, beside the operation and the artifacts.</summary>
    /// <param name="Source">The retained source.</param>
    /// <param name="Replacement">The reviewed replacement target, with the model's record.</param>
    /// <param name="Incoming">The incoming source file.</param>
    /// <param name="RecordRelative">The model import record's project-relative path.</param>
    /// <param name="ResolveScopes">Resolves the cook scopes at discovery time.</param>
    /// <param name="Previous">The previous cook provenance.</param>
    /// <param name="Libraries">The cooked libraries read by the cook.</param>
    private sealed record ReplacementDiscovery(
        ContentCookInput Source,
        SceneImportReplacement Replacement,
        string Incoming,
        string RecordRelative,
        Func<IReadOnlyList<ContentCookScope>> ResolveScopes,
        CookProvenance Previous,
        CookedLibraryReadSet Libraries);
}
