// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Plans native jobs and preserves source-owned output evidence across partial cooks.</summary>
public sealed partial class ContentPipelineService
{
    private static async Task RetainStagingUntilDrain(ContentPipelineTerminationException failure, CookStagingArea staging, CookReferenceRoots? references)
    {
        try
        {
            await failure.DrainCompletion.ConfigureAwait(false);
        }
        finally
        {
            try
            {
                references?.Dispose();
            }
            finally
            {
                await staging.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static CookProvenance BuildProvenance(CookProvenance previous, CookIncrementalPlan plan, CookDependencyGraph graph, IReadOnlyList<ContentCookResult> results, CookInputSnapshot snapshot, ImmutableArray<CookProducedSourceFile> produced)
    {
        var expectedInputs = CookProducedSourceFile.ExpectedInputs(snapshot.Inputs, produced);
        var expectedFiles = expectedInputs.ToDictionary(static file => file.RelativePath, StringComparer.Ordinal);
        var sourceInputs = graph.Assets.ToDictionary(static input => input.AssetUri);
        var roots = previous.Roots.ToDictionary(static root => root.Mount, StringComparer.Ordinal);
        var products = previous.Products.ToDictionary(static product => product.SourceUri);
        var inventories = plan.PriorInventories.ToBuilder();
        var invalidatedRoots = previous.Roots.Where(root => !plan.ValidSharedRoots.Contains(root.Mount)).Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal);
        foreach (var result in results.Where(static value => value.VerifiedRoot is not null))
        {
            var root = result.VerifiedRoot!;
            var changed = result.CookedAssets.Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
            var candidate = result.NativeInventory ?? throw new InvalidDataException("Cooked output omitted its native inventory.");
            if (inventories.TryGetValue(root.Mount, out var priorInventory))
            {
                var oldAssets = priorInventory.Assets.ToDictionary(static asset => asset.VirtualPath, StringComparer.Ordinal);
                foreach (var asset in candidate.Assets.Where(asset => !changed.Contains(asset.VirtualPath)))
                {
                    if (oldAssets.TryGetValue(asset.VirtualPath, out var old)
                        && (asset.Key != old.Key
                            || !string.Equals(asset.DescriptorPath, old.DescriptorPath, StringComparison.Ordinal)
                            || candidate.Files[asset.DescriptorPath] != priorInventory.Files[old.DescriptorPath]))
                    {
                        throw new InvalidDataException($"Unrelated cooked descriptor changed during publication: {asset.VirtualPath}.");
                    }
                }

                if (plan.ValidSharedRoots.Contains(root.Mount))
                {
                    foreach (var output in products.Values.SelectMany(static product => product.Outputs)
                        .Where(output => string.Equals(output.RootMount, root.Mount, StringComparison.Ordinal) && !changed.Contains(output.Asset.VirtualPath)))
                    {
                        if (output.Asset.DescriptorRelativePath is not { } path
                            || !priorInventory.Files.TryGetValue(path, out var expected)
                            || !candidate.Files.TryGetValue(path, out var actual) || actual != expected)
                        {
                            throw new InvalidDataException($"Unrelated cooked output changed during publication: {output.Asset.VirtualPath}.");
                        }
                    }
                }
            }

            if (invalidatedRoots.Remove(root.Mount))
            {
                foreach (var product in products.Values.Where(product => product.Outputs.Any(output => string.Equals(output.RootMount, root.Mount, StringComparison.Ordinal))).ToArray())
                {
                    _ = products.Remove(product.SourceUri);
                }
            }

            inventories[root.Mount] = candidate;
            roots[root.Mount] = root;
            foreach (var source in result.CookedAssets.GroupBy(static asset => asset.SourceAssetUri))
            {
                if (!plan.Fingerprints.TryGetValue(source.Key, out var fingerprint))
                {
                    continue;
                }

                var dependencies = graph.Dependencies.TryGetValue(source.Key, out var discovered) ? discovered
                    : ProceduralGeometryDescriptorService.IsGeneratedBasicShape(source.Key) ? [AssetUris.BuildGeneratedUri("Materials/Default")] : ImmutableArray<Uri>.Empty;
                products[source.Key] = new(source.Key, fingerprint, dependencies, [.. source.Select(asset => new CookProvenance.Output(asset, root.Mount))])
                {
                    SourceInput = sourceInputs.GetValueOrDefault(source.Key),
                    SourceFiles = graph.FileDependencies.TryGetValue(source.Key, out var sourceFiles)
                        ? [.. sourceFiles.Select(path => expectedFiles[path])] : [],
                    DeclaredOutputs = graph.SourceFacts.GetValueOrDefault(source.Key)?.Outputs ?? [],
                    NativeReferences = graph.SourceFacts.GetValueOrDefault(source.Key)?.References ?? [],
                    AuxiliaryFiles = result.AuxiliaryFilesBySource.GetValueOrDefault(source.Key, []),
                    ReuseFingerprint = sourceInputs.TryGetValue(source.Key, out var sourceInput)
                        ? CookIncrementalPlanner.Fingerprint(sourceInput, snapshot.BuildFingerprint, expectedInputs, graph) : fingerprint,
                    CookedDependencies = graph.CookedDependencies.GetValueOrDefault(source.Key, []),
                    ImportedSource = graph.ImportedSources.TryGetValue(source.Key, out var imported) ? imported with
                    {
                        ContentFingerprint = CookDependencyDiscovery.FingerprintImportedContent(graph.FileDependencies[source.Key].Select(path => expectedFiles[path])),
                    } : null,
                    Diagnostics =
                    [
                        .. result.Diagnostics.Where(diagnostic => diagnostic.Severity == Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Warning
                            && (diagnostic.AffectedVirtualPath is null || string.Equals(diagnostic.AffectedVirtualPath, source.Key.AbsolutePath, StringComparison.Ordinal)
                                || source.Any(asset => string.Equals(asset.VirtualPath, diagnostic.AffectedVirtualPath, StringComparison.Ordinal)))),
                    ],
                };
            }
        }

        var retained = products.Values.Where(product => product.Outputs.All(output => inventories.TryGetValue(output.RootMount, out var inventory)
            && output.Asset.DescriptorRelativePath is { } path && inventory.Files.ContainsKey(path)));
        return new(previous.ProjectId, [.. roots.Values.OrderBy(static root => root.Mount, StringComparer.Ordinal)], [.. retained.OrderBy(static product => product.SourceUri.AbsoluteUri, StringComparer.Ordinal)]);
    }

    private async Task<IReadOnlyList<ContentCookInput>> PrepareMissingBuiltinsAsync(
        ContentCookOperation operation,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookIncrementalPlan plan,
        IReadOnlyList<ContentCookInput> dirtyInputs,
        NativeArtifactLease artifacts,
        CookTargetKind targetKind,
        CancellationToken cancellationToken)
    {
        var covered = dirtyInputs.Where(static input => input.Kind == ContentCookAssetKind.Scene)
            .SelectMany(input => graph.SceneDescriptors.GetValueOrDefault(input.AssetUri)?.Dependencies ?? [])
            .Select(static input => input.AssetUri).ToHashSet();

        var missing = CookIncrementalPlan.Builtins(graph).Where(uri => !plan.Reusable.ContainsKey(uri) && !covered.Contains(uri)).ToHashSet();
        if (missing.Count == 0)
        {
            return [];
        }

        var retained = graph.GeneratedSources.Where(input => missing.Remove(input.AssetUri)).ToArray();
        if (missing.Count == 0)
        {
            return retained;
        }

        if (this.engineContentPipelineApi is not IBuiltinGeometryCatalogProvider catalog)
        {
            throw new InvalidOperationException("The native builtin catalog is unavailable.");
        }

        var scope = this.CreateScope(operation.Project, graph.Assets, targetKind) with
        {
            Snapshot = snapshot,
            Artifacts = artifacts,
            PreparationRoot = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"), "builtins"),
        };
        var generated = await new ProceduralGeometryDescriptorService(catalog).EnsureDescriptorsAsync(scope, [.. missing], cancellationToken).ConfigureAwait(false);
        return missing.Any(uri => !generated.Any(input => input.AssetUri == uri))
            ? throw new InvalidDataException("A required generated asset is absent from the native catalog.")
            : [.. retained, .. generated.Select(input => input with { SourceRelativePath = Path.GetRelativePath(operation.Project.ProjectRoot, input.SourceAbsolutePath).Replace('\\', '/') })];
    }

    private async Task<ContentCookResult> ExecuteIncrementalCookAsync(ContentCookOperation operation, CookPublicationReadLease baseline, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CookProvenance previous, Import.ImportedSourceIndex imports, CancellationToken cancellationToken)
    {
        var mounts = operation.Project.AuthoringMounts.Where(static mount => !IsDerivedRootMount(mount))
            .Select(static mount => mount.Name).Concat(previous.Roots.Select(static root => root.Mount)).Distinct(StringComparer.Ordinal).ToArray();
        var inventories = await CookIncrementalPlanner.ReadInventoriesAsync(baseline, mounts, this.engineContentPipelineApi, cancellationToken, artifacts).ConfigureAwait(false);
        var repair = CookRootRepair.Create(baseline, previous, mounts, inventories);
        resolveScopes = this.ExpandRepairScopes(operation, resolveScopes, repair, imports, targetKind);
        var knownOutputs = imports.KnownOutputs.Concat(baseline.ProjectOutputPaths.Select(ToAssetUri)).Distinct().ToArray();
        var libraries = await CookedLibraryReadSet.AcquireAsync(operation.Project, cancellationToken, uri => imports.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency), knownOutputs).ConfigureAwait(false);
        try
        {
            await libraries.ValidateNativeAsync(this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
            var (snapshot, graph) = await this.CaptureScopesAsync(operation, resolveScopes, artifacts, previous, imports, libraries, cancellationToken).ConfigureAwait(false);
            graph = graph with { Builtins = [.. graph.Builtins.Union(repair.Sources.Where(IsGeneratedSource))] };
            var captured = graph.Assets.Select(static input => input.AssetUri).Concat(graph.Builtins).ToHashSet();
            if (!repair.Sources.All(captured.Contains))
            {
                throw new InvalidDataException("Not all sources required to rebuild the damaged content were captured.");
            }

            graph = libraries.Apply(graph);
            if (HasError(graph.Diagnostics))
            {
                throw new CookInputDiscoveryException(graph.Diagnostics);
            }

            snapshot = CookedLibraryReadSet.CaptureDependencies(snapshot, graph);
            return await this.ExecuteCapturedCookAsync(operation, baseline, resolveScopes, targetKind, artifacts, previous, snapshot, graph, libraries, inventories, repair, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            var retained = libraries;
            var drain = failure.DrainCompletion.ContinueWith(
                completed =>
            {
                _ = completed.Exception;
                retained.Dispose();
            },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
            libraries = null;
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
        }
        finally
        {
            libraries?.Dispose();
        }
    }

    private async Task<ContentCookResult> ExecuteCapturedCookAsync(ContentCookOperation operation, CookPublicationReadLease baseline, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CookProvenance previous, CookInputSnapshot snapshot, CookDependencyGraph graph, CookedLibraryReadSet libraries,
        ImmutableDictionary<string, Inspection.CookedInventoryReport> inventories, CookRootRepair repair, CancellationToken cancellationToken)
    {
        var plan = CookIncrementalPlanner.CreatePlan(snapshot.BuildFingerprint, snapshot.Inputs, graph, previous, inventories, repair.EmptyRoots);
        if (resolveScopes().Any(static scope => !scope.AllowImportedSourceChanges))
        {
            var blocked = ChangedImportedSourceDiagnostics(operation.OperationId, graph, previous, plan);
            if (blocked.Length != 0)
            {
                return new(operation.OperationId, targetKind, OperationStatus.Failed, blocked, [], Inspection: null, Validation: null);
            }
        }

        foreach (var asset in plan.ReusedAssets)
        {
            CookRunContext.Report(new(Asset: new(asset.SourceAssetUri, asset.Kind, CookAssetState.Reused)));
        }

        if (plan.IsUpToDate)
        {
            var missing = MissingImportedOutputs(operation.OperationId, graph, plan.ReusedAssets);
            if (missing.Length != 0)
            {
                return new(operation.OperationId, targetKind, OperationStatus.Failed, missing, [], Inspection: null, Validation: null);
            }

            CookRunContext.Report(new(Message: "Already up to date."));
            var unchanged = new ContentCookResult(operation.OperationId, targetKind, plan.Diagnostics.IsEmpty ? OperationStatus.Succeeded : OperationStatus.SucceededWithWarnings, NormalizeDiagnostics(operation.OperationId, plan.Diagnostics), [], Inspection: null, Validation: null)
            {
                IsUpToDate = true,
                ReusedAssets = plan.ReusedAssets,
                InputSnapshot = snapshot,
                InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, cancellationToken).ConfigureAwait(false),
            };
            return await this.publication.PublishObservedBindingsAsync(operation, baseline, libraries, unchanged, cancellationToken).ConfigureAwait(false);
        }

        var dirtyInputs = graph.Assets.Where(input => !plan.Reusable.ContainsKey(input.AssetUri)).ToList();
        var missingScenes = dirtyInputs.Where(input => input.Kind == ContentCookAssetKind.Scene && !graph.SceneDescriptors.ContainsKey(input.AssetUri)).ToArray();
        if (missingScenes.Length != 0)
        {
            var prepared = await new CookSourceAnalyzer(operation, artifacts, this.engineContentPipelineApi, this.manifestBuilder,
                this.sceneDescriptorGenerator, this.cookScopeProvider, cookDocuments)
                .PrepareCapturedScenesAsync(missingScenes, snapshot, graph.GeneratedSources, cancellationToken).ConfigureAwait(false);
            if (HasError(prepared.Diagnostics))
            {
                throw new CookInputDiscoveryException(prepared.Diagnostics);
            }

            graph = graph with
            {
                SceneDescriptors = graph.SceneDescriptors.SetItems(prepared.Sources.Select(static source => KeyValuePair.Create(source.Input.AssetUri, source.Scene!))),
                NativeJobs = graph.NativeJobs.SetItems(prepared.Sources.Select(static source => KeyValuePair.Create(source.Input.AssetUri, source.Job!))),
                GeneratedSources = [.. graph.GeneratedSources, .. prepared.GeneratedSources],
                GeneratedInputs = [.. graph.GeneratedInputs, .. prepared.GeneratedInputs],
            };
        }

        var generated = await this.PrepareMissingBuiltinsAsync(operation, snapshot, graph, plan, dirtyInputs, artifacts, targetKind, cancellationToken).ConfigureAwait(false);
        dirtyInputs.AddRange(generated);
        var admittedGenerated = graph.GeneratedInputs.Select(static input => input.LogicalPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        graph = graph with
        {
            GeneratedInputs = [.. graph.GeneratedInputs, .. await CookInputSnapshotCapture.CaptureGeneratedAsync(
                generated.Where(input => !admittedGenerated.Contains(input.SourceAbsolutePath)), cancellationToken).ConfigureAwait(false)],
        };
        CookStagingArea? staging = null;
        CookReferenceRoots? references = null;
        try
        {
            staging = await this.publication.CreateStagingAsync(operation, baseline, dirtyInputs.Select(static input => input.MountName).Distinct(StringComparer.OrdinalIgnoreCase), repair.EmptyRoots, cancellationToken).ConfigureAwait(false);
            staging.VerifyAcceptedInventories(inventories, repair.EmptyRoots);
            references = await CookReferenceRoots.AcquireAsync(staging, previous, plan, this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
            return await this.CookAndPublishStagingAsync(operation, targetKind, artifacts, resolveScopes, snapshot, graph, previous, plan, dirtyInputs, staging, references, libraries, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure) when (staging is not null)
        {
            var drain = RetainStagingUntilDrain(failure, staging, references);
            staging = null;
            references = null;
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
        }
        finally
        {
            try
            {
                references?.Dispose();
            }
            finally
            {
                if (staging is not null)
                {
                    await staging.DisposeAsync().ConfigureAwait(false);
                }
            }
        }
    }

    private static bool IsGeneratedSource(Uri uri)
        => uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase);

    private Func<IReadOnlyList<ContentCookScope>> ExpandRepairScopes(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookRootRepair repair, Import.ImportedSourceIndex imports, CookTargetKind targetKind)
    {
        if (repair.Sources.IsEmpty)
        {
            return resolveScopes;
        }

        CookRunContext.Report(new(Message: "Rebuilding affected content.", State: CookRunState.Preparing));
        return () =>
        {
            var requested = resolveScopes();
            var included = requested.SelectMany(static scope => scope.Inputs).Select(static input => input.AssetUri).ToHashSet();
            var additional = repair.Sources.Where(uri => !IsGeneratedSource(uri) && !included.Contains(uri)).Select(uri =>
                imports.ResolveScope(this.CreateScope(
                    operation.Project,
                    [CookInputResolver.Resolve(operation.Project, uri, ContentCookInputRole.Primary)], targetKind) with
                {
                    ScopeUri = uri,
                    AllowImportedSourceChanges = requested.All(static scope => scope.AllowImportedSourceChanges),
                }));
            return [.. requested, .. additional];
        };
    }

    private async Task<ContentCookResult> CookAndPublishStagingAsync(
        ContentCookOperation operation,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookProvenance previous,
        CookIncrementalPlan plan,
        List<ContentCookInput> dirtyInputs,
        CookStagingArea staging,
        CookReferenceRoots references,
        CookedLibraryReadSet libraries,
        CancellationToken cancellationToken)
    {
        var results = await this.CookDependencyLayersAsync(operation, targetKind, artifacts, snapshot, graph, previous, plan, dirtyInputs, staging, libraries.OrderRoots(references.Paths), cancellationToken).ConfigureAwait(false);

        var cooked = results.Count == 1 ? results[0] : MergeProjectResults(operation.OperationId, results);
        var result = cooked with
        {
            TargetKind = targetKind,
            InputSnapshot = snapshot,
            ReusedAssets = plan.ReusedAssets,
            Status = cooked.Status == OperationStatus.Succeeded && !plan.Diagnostics.IsEmpty ? OperationStatus.SucceededWithWarnings : cooked.Status,
            Diagnostics = NormalizeDiagnostics(operation.OperationId, cooked.Diagnostics.Concat(plan.Diagnostics)),
        };
        var missingOutputs = MissingImportedOutputs(operation.OperationId, graph, result.CookedAssets.Concat(plan.ReusedAssets));
        if (missingOutputs.Length != 0)
        {
            return result with { Status = OperationStatus.Failed, Diagnostics = [.. result.Diagnostics, .. missingOutputs] };
        }

        if (results.TrueForAll(static value => value.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings)
            && results.TrueForAll(static value => value.VerifiedRoot is not null))
        {
            references.Verify();
            libraries.Verify();
            result = result with { ProducedSourceFiles = await PrepareProducedSourceFilesAsync(snapshot, graph, result, cancellationToken).ConfigureAwait(false) };
            using var sourceOwners = snapshot.SourceReplacement is null && result.ProducedSourceFiles.IsEmpty ? null
                : await cookDocuments.AcquireAsync(snapshot.Inputs.Select(static input => input.SourcePath), cancellationToken).ConfigureAwait(false);
            if (sourceOwners?.Documents.Any(static document => document.IsDirty) == true)
            {
                throw new InvalidOperationException("The retained source has unsaved edits. Save or discard them and review the replacement again.");
            }

            result = await this.publication.PublishAsync(operation, staging, result, BuildProvenance(previous, plan, graph, results, snapshot, result.ProducedSourceFiles), libraries, cancellationToken).ConfigureAwait(false);
            if (!result.IsPublished && resolveScopes().SingleOrDefault(static scope => scope.ImportReplacement is not null) is { } replacement)
            {
                RetainRolledBackReplacement(replacement, snapshot);
            }
        }
        else if (results.TrueForAll(static value => value.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings))
        {
            throw new InvalidDataException("Native validation did not establish output identities for publication.");
        }

        return result with { InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, result.IsPublished ? CancellationToken.None : cancellationToken, result.IsPublished ? result.ProducedSourceFiles : []).ConfigureAwait(false) };
    }
}
