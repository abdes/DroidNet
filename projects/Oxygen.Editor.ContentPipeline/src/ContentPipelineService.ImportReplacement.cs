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
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Cooks reviewed replacement bytes privately before source and output commit together.</summary>
public sealed partial class ContentPipelineService
{
    private static CookDependencyGraph CreateReplacementGraph(ContentCookInput source, NativeSceneImportSettings settings, ImmutableArray<CookSnapshotInput> inputs, string settingsRelative, CookProvenance previous)
        => new(
            [source],
            inputs,
            ImmutableDictionary<Uri, ImmutableArray<Uri>>.Empty.Add(source.AssetUri, []),
            ImmutableDictionary<Uri, ImmutableArray<string>>.Empty.Add(source.AssetUri, [.. inputs.Select(static file => file.RelativePath)]),
            [],
            [],
            [])
        {
            ImportedSources = ImmutableDictionary<Uri, ImportedSourceDependencyState>.Empty.Add(
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
        var sourceRoot = settings.ResolveFile(operation.Project.ProjectRoot, ".").TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return bundle.Files.Select(file =>
        {
            var primary = string.Equals(file.RelativePath, bundle.PrimaryRelativePath, StringComparison.OrdinalIgnoreCase);
            var target = primary ? source.SourceAbsolutePath : Path.GetFullPath(Path.Combine(Path.GetDirectoryName(source.SourceAbsolutePath)!, Path.GetRelativePath(Path.GetDirectoryName(incoming)!, file.SourcePath)));
            return !target.StartsWith(sourceRoot, StringComparison.OrdinalIgnoreCase)
                && !(file.Kind == CookSnapshotInputKind.Probe && file.Metadata?.IsDirectory == true
                    && string.Equals(target, Path.TrimEndingDirectorySeparator(sourceRoot), StringComparison.OrdinalIgnoreCase))
                ? throw new InvalidDataException("The incoming dependency layout cannot preserve the existing source location. Import it into a new destination.")
                : file with { AssetUri = primary ? source.AssetUri : null, RelativePath = Path.GetRelativePath(operation.Project.ProjectRoot, target).Replace('\\', '/') };
        }).ToArray();
    }

    private static void RetainRolledBackReplacement(ContentCookScope scope, CookInputSnapshot snapshot)
    {
        var request = scope.ImportReplacement!;
        var replacement = request.Replacement!;
        var candidate = Path.Combine(snapshot.Operation.Project.ProjectRoot, ".build", "cook", snapshot.Operation.OperationId.ToString("N"), "discarded-source", replacement.BundleName, replacement.Settings.PrimaryRelativePath);
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

    private async Task<(CookInputSnapshot snapshot, CookDependencyGraph graph)> CaptureReplacementAsync(
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
        if (!string.Equals(Path.GetExtension(incoming), Path.GetExtension(source.SourceAbsolutePath), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Replacement must use the existing source format. Import a different format into a new destination.");
        }

        var settingsRelative = source.SourceRelativePath + NativeSceneImportSettings.SidecarSuffix;
        CookDependencyGraph? graph = null;
        ImmutableArray<CookSnapshotInput> replacementFiles = [];
        var capture = await new CookInputSnapshotCapture(cookDocuments, this.cookCoordinator).CaptureAsync(
            operation,
            async token =>
            {
                var discovered = await this.DiscoverChangedImportedSourceAsync(operation, source with { SourceAbsolutePath = incoming }, original, artifacts, token).ConfigureAwait(false);
                replacementFiles = [.. MapReplacementFiles(operation, source, original, incoming, discovered)];
                var prepared = CreateReplacementGraph(source, original with
                {
                    SourceHash = replacementFiles.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash,
                }, replacementFiles, settingsRelative, previous);
                var scopes = resolveScopes();
                graph = await this.CreateDependencyDiscovery(operation, artifacts, previous, imports, libraries)
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
        var replacementPaths = replacementFiles.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
        var inputs = snapshot.Inputs.Select(file => replacementPaths.Contains(file.RelativePath)
            ? file with { SourcePath = Path.Combine(operation.Project.ProjectRoot, file.RelativePath) } : file).ToImmutableArray();
        var settings = original with
        {
            SourceHash = inputs.Single(file => file.AssetUri == source.AssetUri).DiscoveryHash,
            Files = [.. inputs.Where(file => file.Kind == CookSnapshotInputKind.File && replacementPaths.Contains(file.RelativePath))
                .Select(file => Path.GetRelativePath(original.ResolveFile(operation.Project.ProjectRoot, "."), file.SourcePath).Replace('\\', '/')).Order(StringComparer.Ordinal)],
        };
        var bytes = settings.ToBytes();
        if (inputs.Any(file => string.Equals(file.RelativePath, settingsRelative, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidDataException("An incoming dependency conflicts with the existing import settings path.");
        }

        await File.WriteAllBytesAsync(Path.Combine(snapshot.InputRoot, settingsRelative), bytes, cancellationToken).ConfigureAwait(false);
        inputs = inputs.Add(new(null, Path.Combine(operation.Project.ProjectRoot, settingsRelative), settingsRelative, Convert.ToHexString(SHA256.HashData(bytes)))
        {
            Metadata = CookSavedSourceReader.ReadMetadata(Path.Combine(snapshot.InputRoot, settingsRelative)),
        });
        inputs = [.. inputs.OrderBy(static file => file.RelativePath, StringComparer.Ordinal)];
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
        var replacementGraph = CreateReplacementGraph(source, settings,
            [.. inputs.Where(file => replacementPaths.Contains(file.RelativePath) || string.Equals(file.RelativePath, settingsRelative, StringComparison.Ordinal))], settingsRelative, previous);
        graph = graph! with
        {
            Files = inputs,
            FileDependencies = graph.FileDependencies.SetItem(source.AssetUri, replacementGraph.FileDependencies[source.AssetUri]),
            ImportedSources = graph.ImportedSources.SetItem(source.AssetUri, replacementGraph.ImportedSources[source.AssetUri]),
        };
        var job = this.manifestBuilder.BuildJob(source, [], settings);
        var analysis = await this.engineContentPipelineApi.AnalyzeSourcesAsync(
            new(
            operation.OperationId,
            operation.Project.ProjectRoot,
            Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")),
            [job])
            { Artifacts = artifacts, CapturedInputs = snapshot.CreateNativeInputs() }, cancellationToken).ConfigureAwait(false);
        if (!analysis.Complete)
        {
            var issues = analysis.Jobs.SelectMany(static result => result.Diagnostics).ToArray();
            throw issues.Length != 0 ? new CookInputDiscoveryException(issues)
                : new InvalidDataException("Native analysis of the captured replacement did not complete.");
        }

        var facts = analysis.Jobs.Single();
        graph = graph with
        {
            NativeJobs = graph.NativeJobs.SetItem(source.AssetUri, job),
            SourceFacts = graph.SourceFacts.SetItem(
                source.AssetUri,
                new(source, facts.Outputs, facts.References,
                    [.. inputs.Where(file => graph.FileDependencies[source.AssetUri].Contains(file.RelativePath, StringComparer.Ordinal))])
                { Job = job }),
        };
        return (snapshot, graph);
    }

    private static void RequireCompleteReplacementBundle(ContentCookOperation operation, NativeSceneImportSettings settings,
        ImmutableArray<CookSnapshotInput> replacementFiles, CookDependencyGraph graph)
    {
        var root = settings.ResolveFile(operation.Project.ProjectRoot, ".").TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var incoming = replacementFiles.Select(static file => file.RelativePath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var missing = graph.Files.FirstOrDefault(file => !file.IsAbsent && !incoming.Contains(file.RelativePath)
            && Path.GetFullPath(Path.Combine(operation.Project.ProjectRoot, file.RelativePath)).StartsWith(root, StringComparison.OrdinalIgnoreCase));
        if (missing is not null)
        {
            var owners = graph.FileDependencies.Where(pair => pair.Value.Contains(missing.RelativePath, StringComparer.Ordinal))
                .Select(static pair => pair.Key.AbsolutePath);
            throw new CookInputDiscoveryException([new()
            {
                OperationId = operation.OperationId,
                Domain = Oxygen.Managed.Core.Diagnostics.FailureDomain.AssetCook,
                Severity = Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error,
                Code = "asset_cook.replacement_dependency_missing",
                AffectedPath = missing.SourcePath,
                Message = $"{string.Join(", ", owners)} requires '{missing.RelativePath}', which the replacement would remove. Include it in the replacement or update the dependent source before replacing the model.",
            }
            ]);
        }
    }

    private async Task VerifyReplacementTargetAsync(ContentCookOperation operation, SceneImportReplacement replacement, ImportedSourceIndex imports, CancellationToken cancellationToken)
    {
        var root = ImportSourceRetention.ResolveDestination(operation.Project, replacement.BundleName);
        using var owners = await cookDocuments.AcquireAsync(replacement.Settings.Files.Append(replacement.Settings.PrimaryRelativePath + NativeSceneImportSettings.SidecarSuffix).Select(path => Path.Combine(root, path)), cancellationToken).ConfigureAwait(false);
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
        if (settings.OutputPrefixes.Any(prefix => imports.ResolveFolder(operation.Project, new Uri("asset://" + prefix)).Any(input => input.AssetUri != replacement.SourceUri)))
        {
            throw new InvalidDataException("The replacement destination overlaps another retained source. Resolve its ownership before replacing this model.");
        }

        var mount = operation.Project.AuthoringMounts.Single(mount => string.Equals(mount.Name, settings.MountPoint, StringComparison.OrdinalIgnoreCase));
        var conflict = settings.OutputDirectories.Select(directory => Path.Combine(operation.Project.ProjectRoot, mount.RelativePath, directory))
            .Where(Directory.Exists).SelectMany(static directory => Directory.EnumerateFiles(directory, "*.json", SearchOption.AllDirectories))
            .FirstOrDefault(static path => Path.GetExtension(Path.GetFileNameWithoutExtension(path)).ToUpperInvariant() is ".OMAT" or ".OGEO" or ".OSCENE");
        if (conflict is not null)
        {
            throw new InvalidDataException($"The replacement destination contains authored asset '{conflict}'. Keep it in a separate destination before replacing the model.");
        }
    }
}
