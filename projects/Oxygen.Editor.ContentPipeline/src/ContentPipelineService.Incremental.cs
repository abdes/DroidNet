// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
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
        var roots = previous.Roots.Where(root => !plan.DroppedRoots.Contains(root.Mount)).ToDictionary(static root => root.Mount, StringComparer.Ordinal);
        var products = previous.Products.Where(product => !plan.RemovedSources.Contains(product.SourceUri)
                && !product.Outputs.Any(output => plan.DroppedRoots.Contains(output.RootMount)))
            .ToDictionary(static product => product.SourceUri);
        var inventories = plan.PriorInventories.ToBuilder();
        var invalidatedRoots = previous.Roots.Where(root => !plan.ValidSharedRoots.Contains(root.Mount)).Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal);
        foreach (var result in results.Where(static value => value.VerifiedRoot is not null))
        {
            var root = result.VerifiedRoot!;
            var changed = result.CookedAssets.Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
            var candidate = result.NativeInventory ?? throw new InvalidDataException("Cooked output omitted its native inventory.");
            if (inventories.TryGetValue(root.Mount, out var priorInventory))
            {
                EnsureUnrelatedOutputsUnchanged(plan, root, changed, candidate, priorInventory, products.Values);
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
                if (plan.Fingerprints.TryGetValue(source.Key, out var fingerprint))
                {
                    products[source.Key] = CreateProduct(source, fingerprint, root, result, graph, snapshot, expectedInputs, expectedFiles, sourceInputs);
                }
            }
        }

        var retained = products.Values.Where(product => product.Outputs.All(output => inventories.TryGetValue(output.RootMount, out var inventory)
            && output.Asset.DescriptorRelativePath is { } path && inventory.Files.ContainsKey(path)));
        return new(previous.ProjectId, [.. roots.Values.OrderBy(static root => root.Mount, StringComparer.Ordinal)], [.. retained.OrderBy(static product => product.SourceUri)]);
    }

    private static void EnsureUnrelatedOutputsUnchanged(
        CookIncrementalPlan plan,
        CookProvenance.Root root,
        HashSet<string> changed,
        Inspection.CookedInventoryReport candidate,
        Inspection.CookedInventoryReport priorInventory,
        IEnumerable<CookProvenance.Product> products)
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

        if (!plan.ValidSharedRoots.Contains(root.Mount))
        {
            return;
        }

        foreach (var output in products.SelectMany(static product => product.Outputs)
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

    private static CookProvenance.Product CreateProduct(
        IGrouping<AssetPath, ContentCookedAsset> source,
        string fingerprint,
        CookProvenance.Root root,
        ContentCookResult result,
        CookDependencyGraph graph,
        CookInputSnapshot snapshot,
        ImmutableArray<CookSnapshotInput> expectedInputs,
        Dictionary<string, CookSnapshotInput> expectedFiles,
        Dictionary<AssetPath, ContentCookInput> sourceInputs)
    {
        var dependencies = graph.Dependencies.GetValueOrDefault(source.Key, []);
        return new(source.Key, fingerprint, dependencies, [.. source.Select(asset => new CookProvenance.Output(asset, root.Mount))])
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
                    && (diagnostic.AffectedVirtualPath is null || string.Equals(diagnostic.AffectedVirtualPath, source.Key.VirtualPath, StringComparison.Ordinal)
                        || source.Any(asset => string.Equals(asset.VirtualPath, diagnostic.AffectedVirtualPath, StringComparison.Ordinal)))),
            ],
        };
    }

    private async Task<ContentCookResult> ExecuteIncrementalCookAsync(ContentCookOperation operation, CookPublicationReadLease baseline, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CookProvenance previous, CancellationToken cancellationToken)
    {
        var mounts = operation.Project.AuthoringMounts.Where(static mount => !ProjectOutputFolders.Contains(mount))
            .Select(static mount => mount.Name).Concat(previous.Roots.Select(static root => root.Mount)).Distinct(StringComparer.Ordinal).ToArray();
        var inventories = await CookIncrementalPlanner.ReadInventoriesAsync(baseline, mounts, this.engineContentPipelineApi, cancellationToken, artifacts).ConfigureAwait(false);
        var repair = CookRootRepair.Create(baseline, previous, mounts, inventories);
        resolveScopes = this.ExpandRepairScopes(operation, resolveScopes, repair, targetKind);
        var libraries = await CookedLibraryReadSet.AcquireAsync(operation.Project, this.engineContent, cancellationToken, uri => Import.ImportedOutputs.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency), [.. previous.ProducedAssets]).ConfigureAwait(false);
        try
        {
            await libraries.ValidateNativeAsync(this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
            var (snapshot, graph) = await this.CaptureScopesAsync(operation, resolveScopes, artifacts, previous, libraries, cancellationToken).ConfigureAwait(false);
            var captured = graph.Assets.Select(static input => input.AssetUri).ToHashSet();
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

    private async Task<ContentCookResult> ExecuteCapturedCookAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        CookProvenance previous,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookedLibraryReadSet libraries,
        ImmutableDictionary<string, Inspection.CookedInventoryReport> inventories,
        CookRootRepair repair,
        CancellationToken cancellationToken)
    {
        // A root reconstructed or vacated, that nothing in this cook goes into, leaves the publication.
        var dropped = repair.EmptyRoots.Union(repair.VacatedRoots)
            .Where(mount => !graph.Assets.Any(input => string.Equals(input.MountName, mount, StringComparison.Ordinal))).ToArray();
        var plan = CookIncrementalPlanner.CreatePlan(snapshot.BuildFingerprint, snapshot.Inputs, graph, previous, inventories, repair.EmptyRoots, repair.RemovedSources, repair.Sources) with
        {
            DroppedRoots = dropped.ToImmutableHashSet(StringComparer.Ordinal),
            Removals = repair.Removals.RemoveRange(dropped),
        };
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
            return await this.CompleteUpToDateAsync(operation, baseline, targetKind, libraries, snapshot, graph, plan, resolveScopes, cancellationToken).ConfigureAwait(false);
        }

        return await this.CookDirtyInputsAsync(operation, baseline, resolveScopes, targetKind, artifacts, previous, snapshot, graph, libraries, inventories, repair, plan, cancellationToken).ConfigureAwait(false);
    }

    private async Task<ContentCookResult> CookDirtyInputsAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        CookProvenance previous,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookedLibraryReadSet libraries,
        ImmutableDictionary<string, Inspection.CookedInventoryReport> inventories,
        CookRootRepair repair,
        CookIncrementalPlan plan,
        CancellationToken cancellationToken)
    {
        var dirtyInputs = graph.Assets.Where(input => !plan.Reusable.ContainsKey(input.AssetUri)).ToList();
        var missingScenes = dirtyInputs.Where(input => input.Kind == ContentCookAssetKind.Scene && !graph.SceneDescriptors.ContainsKey(input.AssetUri)).ToArray();
        if (missingScenes.Length != 0)
        {
            graph = await this.PrepareMissingScenesAsync(operation, artifacts, snapshot, graph, missingScenes, cancellationToken).ConfigureAwait(false);
        }

        return await this.CookStagedAsync(operation, baseline, resolveScopes, targetKind, artifacts, previous, snapshot, graph, libraries, inventories, repair, plan, dirtyInputs, cancellationToken).ConfigureAwait(false);
    }

    private async Task<ContentCookResult> CookStagedAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        CookProvenance previous,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookedLibraryReadSet libraries,
        ImmutableDictionary<string, Inspection.CookedInventoryReport> inventories,
        CookRootRepair repair,
        CookIncrementalPlan plan,
        List<ContentCookInput> dirtyInputs,
        CancellationToken cancellationToken)
    {
        CookStagingArea? staging = null;
        CookReferenceRoots? references = null;
        try
        {
            var mounts = dirtyInputs.Select(static input => input.MountName).Concat(plan.Removals.Keys).Distinct(StringComparer.Ordinal);
            staging = await this.publication.CreateStagingAsync(operation, baseline, mounts, repair.EmptyRoots, cancellationToken).ConfigureAwait(false);
            staging.VerifyAcceptedInventories(inventories, repair.EmptyRoots);
            var removals = await this.RemoveRetiredOutputsAsync(operation, targetKind, artifacts, staging, plan, cancellationToken).ConfigureAwait(false);
            references = await CookReferenceRoots.AcquireAsync(staging, previous, plan, this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
            return await this.CookAndPublishStagingAsync(operation, targetKind, artifacts, resolveScopes, snapshot, graph, previous, plan, dirtyInputs, staging, references, libraries, removals, cancellationToken).ConfigureAwait(false);
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

    private async Task<ContentCookResult> CompleteUpToDateAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        CookTargetKind targetKind,
        CookedLibraryReadSet libraries,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookIncrementalPlan plan,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CancellationToken cancellationToken)
    {
        var missing = MissingImportedOutputs(operation.OperationId, graph, plan.ReusedAssets);
        if (missing.Length != 0)
        {
            return new(operation.OperationId, targetKind, OperationStatus.Failed, missing, [], Inspection: null, Validation: null);
        }

        CookRunContext.Report(new(Message: plan.DroppedRoots.IsEmpty
            ? "Already up to date."
            : $"Removed the cooked content of {string.Join(", ", plan.DroppedRoots.Order(StringComparer.Ordinal))}: nothing is left to cook there."));
        var unchanged = new ContentCookResult(operation.OperationId, targetKind, plan.Diagnostics.IsEmpty ? OperationStatus.Succeeded : OperationStatus.SucceededWithWarnings, NormalizeDiagnostics(operation.OperationId, plan.Diagnostics), [], Inspection: null, Validation: null)
        {
            IsUpToDate = true,
            ReusedAssets = plan.ReusedAssets,
            InputSnapshot = snapshot,
            InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, cancellationToken).ConfigureAwait(false),
        };
        return await this.publication.PublishObservedBindingsAsync(operation, baseline, libraries, unchanged, plan.DroppedRoots, cancellationToken).ConfigureAwait(false);
    }

    private async Task<CookDependencyGraph> PrepareMissingScenesAsync(
        ContentCookOperation operation,
        NativeArtifactLease artifacts,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        ContentCookInput[] missingScenes,
        CancellationToken cancellationToken)
    {
        var prepared = await new CookSourceAnalyzer(
                operation,
                artifacts,
                this.engineContentPipelineApi,
                this.manifestBuilder,
                this.sceneDescriptorGenerator,
                this.cookScopeProvider)
            .PrepareCapturedScenesAsync(missingScenes, snapshot, cancellationToken).ConfigureAwait(false);
        if (HasError(prepared.Diagnostics))
        {
            throw new CookInputDiscoveryException(prepared.Diagnostics);
        }

        return graph with
        {
            SceneDescriptors = graph.SceneDescriptors.SetItems(prepared.Sources.Select(static source => KeyValuePair.Create(source.Input.AssetUri, source.Scene!))),
            NativeJobs = graph.NativeJobs.SetItems(prepared.Sources.Select(static source => KeyValuePair.Create(source.Input.AssetUri, source.Job!))),
            GeneratedInputs = [.. graph.GeneratedInputs, .. prepared.GeneratedInputs],
        };
    }

    private Func<IReadOnlyList<ContentCookScope>> ExpandRepairScopes(
        ContentCookOperation operation,
        Func<IReadOnlyList<ContentCookScope>> resolveScopes,
        CookRootRepair repair,
        CookTargetKind targetKind)
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
            var additional = repair.Sources.Where(uri => !included.Contains(uri)).Select(uri =>
            {
                var repairScope = this.CreateScope(
                    operation.Project,
                    [CookInputResolver.Resolve(operation.Project, uri, ContentCookInputRole.Primary)],
                    targetKind);
                return Import.ImportedOutputs.ResolveScope(repairScope with
                {
                    ScopeUri = uri,
                    AllowImportedSourceChanges = requested.All(static scope => scope.AllowImportedSourceChanges),
                });
            });
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
        IReadOnlyList<ContentCookResult> removals,
        CancellationToken cancellationToken)
    {
        // Removal results come first: a later cook into the same root verifies it again and supersedes them.
        List<ContentCookResult> results = [.. removals, .. await this.CookDependencyLayersAsync(operation, targetKind, artifacts, snapshot, graph, previous, plan, dirtyInputs, staging, libraries.OrderRoots(references.Paths), ContentCookScope.RunOf(resolveScopes()), cancellationToken).ConfigureAwait(false)];

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
