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
    private static Task RetainStagingUntilDrain(ContentPipelineTerminationException failure, CookStagingArea staging, CookReferenceRoots? references)
        => failure.DrainCompletion.ContinueWith(
            completed =>
            {
                _ = completed.Exception;
                try
                {
                    try
                    {
                        references?.Dispose();
                    }
                    finally
                    {
                        staging.Dispose();
                    }
                }
                catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException)
                {
                    failure.Data["StagingCleanupFailure"] = cleanup.Message;
                }
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);

    private static CookProvenance BuildProvenance(CookProvenance previous, CookIncrementalPlan plan, CookDependencyGraph graph, IReadOnlyList<ContentCookResult> results)
    {
        var roots = previous.Roots.ToDictionary(static root => root.Mount, StringComparer.Ordinal);
        var products = previous.Products.ToDictionary(static product => product.SourceUri);
        var invalidatedRoots = previous.Roots.Where(root => !plan.ValidSharedRoots.Contains(root.Mount)).Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal);
        foreach (var result in results.Where(static value => value.VerifiedRoot is not null))
        {
            var root = result.VerifiedRoot!;
            var changed = result.CookedAssets.Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
            if (roots.TryGetValue(root.Mount, out var oldRoot))
            {
                var oldAssets = oldRoot.Assets.ToDictionary(static asset => asset.Entry.VirtualPath, StringComparer.Ordinal);
                root = root with
                {
                    // An unrelated descriptor must still match its previous proof; do not bless external edits as cooked output.
                    Assets =
                    [
                        .. root.Assets.Select(asset => !changed.Contains(asset.Entry.VirtualPath)
                            && oldAssets.TryGetValue(asset.Entry.VirtualPath, out var old) ? old : asset),
                    ],
                };
                if (invalidatedRoots.Remove(root.Mount))
                {
                    foreach (var product in products.Values.Where(product => product.Outputs.Any(output => string.Equals(output.RootMount, root.Mount, StringComparison.Ordinal))).ToArray())
                    {
                        _ = products.Remove(product.SourceUri);
                    }
                }
            }

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
                    CookedDependencies = [.. graph.NativeReferences.GetValueOrDefault(source.Key, []).Where(graph.CookedDependencies.ContainsKey).Select(uri => graph.CookedDependencies[uri])],
                    ImportedSource = graph.ImportedSources.GetValueOrDefault(source.Key),
                    Diagnostics =
                    [
                        .. result.Diagnostics.Where(diagnostic => diagnostic.Severity == Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Warning
                            && (diagnostic.AffectedVirtualPath is null || string.Equals(diagnostic.AffectedVirtualPath, source.Key.AbsolutePath, StringComparison.Ordinal)
                                || source.Any(asset => string.Equals(asset.VirtualPath, diagnostic.AffectedVirtualPath, StringComparison.Ordinal)))),
                    ],
                };
            }
        }

        var retained = products.Values.Where(product => product.Outputs.All(output => roots.TryGetValue(output.RootMount, out var root)
            && root.Assets.Any(asset => string.Equals(asset.Entry.VirtualPath, output.Asset.VirtualPath, StringComparison.Ordinal))));
        return new(1, previous.ProjectId, [.. roots.Values.OrderBy(static root => root.Mount, StringComparer.Ordinal)], [.. retained.OrderBy(static product => product.SourceUri.AbsoluteUri, StringComparer.Ordinal)]);
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
            .SelectMany(input => graph.Dependencies[input.AssetUri]).Where(ProceduralGeometryDescriptorService.IsGeneratedBasicShape).ToHashSet();
        if (covered.Count != 0)
        {
            _ = covered.Add(AssetUris.BuildGeneratedUri("Materials/Default"));
        }

        var missing = CookIncrementalPlan.Builtins(graph).Where(uri => !plan.Reusable.ContainsKey(uri) && !covered.Contains(uri)).ToArray();
        if (missing.Length == 0)
        {
            return [];
        }

        if (this.engineContentPipelineApi is not IBuiltinGeometryCatalogProvider catalog)
        {
            throw new InvalidOperationException("The native builtin catalog is unavailable.");
        }

        var scope = this.CreateScope(operation.Project, graph.Assets, targetKind) with { Snapshot = snapshot, Artifacts = artifacts };
        var generated = await new ProceduralGeometryDescriptorService(catalog).EnsureDescriptorsAsync(scope, missing, cancellationToken).ConfigureAwait(false);
        return missing.Any(uri => !generated.Any(input => input.AssetUri == uri))
            ? throw new InvalidDataException("A required generated asset is absent from the native catalog.") : generated;
    }

    private async Task<ContentCookResult> ExecuteIncrementalCookAsync(ContentCookOperation operation, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CookProvenance previous, Import.ImportedSourceIndex imports, CancellationToken cancellationToken)
    {
        var libraries = await CookedLibraryReadSet.AcquireAsync(operation.Project, cancellationToken, uri => imports.ResolveOutput(operation.Project, uri, ContentCookInputRole.Dependency)).ConfigureAwait(false);
        try
        {
            var (snapshot, graph) = await this.CaptureScopesAsync(operation, resolveScopes, artifacts, previous, imports, libraries, cancellationToken).ConfigureAwait(false);
            graph = libraries.Apply(graph);
            if (HasError(graph.Diagnostics))
            {
                throw new CookInputDiscoveryException(graph.Diagnostics);
            }

            snapshot = CookedLibraryReadSet.CaptureDependencies(snapshot, graph);
            return await this.ExecuteCapturedCookAsync(operation, resolveScopes, targetKind, artifacts, previous, snapshot, graph, libraries, cancellationToken).ConfigureAwait(false);
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

    private async Task<ContentCookResult> ExecuteCapturedCookAsync(ContentCookOperation operation, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CookProvenance previous, CookInputSnapshot snapshot, CookDependencyGraph graph, CookedLibraryReadSet libraries, CancellationToken cancellationToken)
    {
        var plan = await CookIncrementalPlanner.PlanAsync(snapshot, graph, previous, cancellationToken).ConfigureAwait(false);
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
            return new(operation.OperationId, targetKind, plan.Diagnostics.IsEmpty ? OperationStatus.Succeeded : OperationStatus.SucceededWithWarnings, NormalizeDiagnostics(operation.OperationId, plan.Diagnostics), [], Inspection: null, Validation: null)
            {
                IsUpToDate = true,
                ReusedAssets = plan.ReusedAssets,
                InputSnapshot = snapshot,
                InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, cancellationToken).ConfigureAwait(false),
            };
        }

        var dirtyInputs = graph.Assets.Where(input => !plan.Reusable.ContainsKey(input.AssetUri)).ToList();
        await libraries.ValidateNativeAsync(this.engineContentPipelineApi, cancellationToken).ConfigureAwait(false);
        dirtyInputs.AddRange(await this.PrepareMissingBuiltinsAsync(operation, snapshot, graph, plan, dirtyInputs, artifacts, targetKind, cancellationToken).ConfigureAwait(false));
        var staging = await CookStagingArea.CreateAsync(operation, dirtyInputs.Select(static input => input.MountName).Distinct(StringComparer.OrdinalIgnoreCase), cancellationToken).ConfigureAwait(false);
        CookReferenceRoots? references = null;
        try
        {
            references = await CookReferenceRoots.AcquireAsync(operation.Project, staging, previous, plan, cancellationToken).ConfigureAwait(false);
            return await this.CookAndPublishStagingAsync(operation, targetKind, artifacts, resolveScopes, snapshot, graph, previous, plan, dirtyInputs, staging, references, libraries, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            var drain = RetainStagingUntilDrain(failure, staging, references);
            staging = null;
            references = null;
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
        }
        finally
        {
            references?.Dispose();
            staging?.Dispose();
        }
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
            TargetKind = targetKind, InputSnapshot = snapshot, ReusedAssets = plan.ReusedAssets,
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
            using var sourceOwners = snapshot.SourceReplacement is null ? null
                : await cookDocuments.AcquireAsync(snapshot.Inputs.Select(static input => input.SourcePath), cancellationToken).ConfigureAwait(false);
            if (sourceOwners?.Documents.Any(static document => document.IsDirty) == true)
            {
                throw new InvalidOperationException("The retained source has unsaved edits. Save or discard them and review the replacement again.");
            }

            result = await this.publication.PublishAsync(operation, staging, result, BuildProvenance(previous, plan, graph, results), cancellationToken).ConfigureAwait(false);
            if (!result.IsPublished && resolveScopes().SingleOrDefault(static scope => scope.ImportReplacement is not null) is { } replacement)
            {
                RetainRolledBackReplacement(replacement, snapshot);
            }
        }
        else if (results.TrueForAll(static value => value.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings))
        {
            throw new InvalidDataException("Native validation did not establish output identities for publication.");
        }

        return result with { InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, result.IsPublished ? CancellationToken.None : cancellationToken).ConfigureAwait(false) };
    }
}
