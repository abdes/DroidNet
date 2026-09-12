// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Plans native jobs and preserves source-owned output evidence across partial cooks.</summary>
public sealed partial class ContentPipelineService
{
    private static CookProvenance BuildProvenance(CookProvenance previous, CookIncrementalPlan plan, CookDependencyGraph graph, IReadOnlyList<ContentCookResult> results)
    {
        var roots = previous.Roots.ToDictionary(static root => root.Mount, StringComparer.Ordinal);
        var products = previous.Products.ToDictionary(static product => product.SourceUri);
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
                if (!plan.ValidSharedRoots.Contains(root.Mount))
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

    private async Task<ContentCookResult> ExecuteIncrementalCookAsync(ContentCookOperation operation, Func<IReadOnlyList<ContentCookScope>> resolveScopes, CookTargetKind targetKind, NativeArtifactLease artifacts, CancellationToken cancellationToken)
    {
        var (snapshot, graph) = await this.CaptureScopesAsync(operation, resolveScopes, artifacts.Fingerprint, cancellationToken).ConfigureAwait(false);
        var (previous, version) = await this.provenanceStore.ReadAsync(operation.Project, cancellationToken).ConfigureAwait(false);
        var plan = await CookIncrementalPlanner.PlanAsync(snapshot, graph, previous, cancellationToken).ConfigureAwait(false);
        foreach (var asset in plan.ReusedAssets)
        {
            CookRunContext.Report(new(Asset: new(asset.SourceAssetUri, asset.Kind, CookAssetState.Reused)));
        }

        if (plan.IsUpToDate)
        {
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
        dirtyInputs.AddRange(await this.PrepareMissingBuiltinsAsync(operation, snapshot, graph, plan, dirtyInputs, artifacts, targetKind, cancellationToken).ConfigureAwait(false));
        var results = new List<ContentCookResult>();
        foreach (var mount in dirtyInputs.GroupBy(static input => input.MountName, StringComparer.OrdinalIgnoreCase))
        {
            var inputs = mount.Select(input => input with { SourceAbsolutePath = Path.Combine(snapshot.InputRoot, input.SourceRelativePath) }).ToArray();
            var scope = this.CreateScope(operation.Project, inputs, targetKind) with { Snapshot = snapshot, Artifacts = artifacts, ReusableSources = plan.Reusable.Keys.ToImmutableHashSet() };
            results.Add(await this.CookMixedInputsAsync(operation.OperationId, scope, cancellationToken).ConfigureAwait(false));
        }

        var result = results.Count == 1 ? results[0] : MergeProjectResults(operation.OperationId, results);
        if (results.TrueForAll(static value => value.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings) && results.Exists(static value => value.VerifiedRoot is not null))
        {
            await this.provenanceStore.WriteAsync(operation.Project, BuildProvenance(previous, plan, graph, results), version, cancellationToken).ConfigureAwait(false);
        }

        return result with
        {
            TargetKind = targetKind,
            Status = result.Status == OperationStatus.Succeeded && !plan.Diagnostics.IsEmpty ? OperationStatus.SucceededWithWarnings : result.Status,
            Diagnostics = NormalizeDiagnostics(operation.OperationId, result.Diagnostics.Concat(plan.Diagnostics)),
            InputSnapshot = snapshot,
            ReusedAssets = plan.ReusedAssets,
            InputsAreCurrent = await this.InputsAreCurrentAsync(snapshot, graph, resolveScopes, cancellationToken).ConfigureAwait(false),
        };
    }
}
