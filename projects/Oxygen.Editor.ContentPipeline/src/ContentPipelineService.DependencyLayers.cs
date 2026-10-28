// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Models;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Orders source work across output mounts while allowing independent branches to finish.</summary>
public sealed partial class ContentPipelineService
{
    private static ImmutableArray<AssetPath> SourceDependencies(ContentCookInput input, CookDependencyGraph graph)
        => graph.Dependencies.GetValueOrDefault(input.AssetUri, []);

    private static ContentCookInput[] ReadyInputs(Dictionary<AssetPath, ContentCookInput> remaining, CookDependencyGraph graph)
    {
        var ready = remaining.Values.Where(input => SourceDependencies(input, graph).All(uri => !remaining.ContainsKey(uri))).Select(static input => input.AssetUri).ToHashSet();
        bool expanded;
        do
        {
            expanded = false;
            foreach (var input in remaining.Values.Where(input => !ready.Contains(input.AssetUri)))
            {
                if (SourceDependencies(input, graph).All(uri => !remaining.TryGetValue(uri, out var dependency)
                    || (ready.Contains(uri) && string.Equals(dependency.MountName, input.MountName, StringComparison.Ordinal))))
                {
                    _ = ready.Add(input.AssetUri);
                    expanded = true;
                }
            }
        }
        while (expanded);

        return [.. remaining.Values.Where(input => ready.Contains(input.AssetUri))];
    }

    private static ContentCookResult SkipDependentInputs(ContentCookOperation operation, CookTargetKind targetKind, IReadOnlyList<ContentCookInput> inputs, bool cycle)
    {
        var diagnostics = inputs.Select(input => new DiagnosticRecord
        {
            OperationId = operation.OperationId,
            Domain = FailureDomain.AssetCook,
            Severity = cycle ? DiagnosticSeverity.Error : DiagnosticSeverity.Warning,
            Code = cycle ? "asset_cook.dependency_cycle" : "asset_cook.dependency_failed",
            AffectedVirtualPath = input.AssetUri.VirtualPath,
            Message = cycle ? $"A dependency cycle prevents cooking '{input.AssetUri}'." : $"Skipped '{input.AssetUri}' because a required asset failed.",
        }).ToArray();
        foreach (var input in inputs)
        {
            CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Skipped)));
        }

        return new(operation.OperationId, targetKind, OperationStatus.Failed, diagnostics, [], Inspection: null, Validation: null);
    }

    // An input of a failed cook fails when an error names it, an error names none of the layer's inputs, or it
    // produced nothing; every other input of the layer is complete.
    private static void RecordLayerOutcome(
        ContentCookResult result,
        ContentCookInput[] inputs,
        Dictionary<AssetPath, ContentCookInput> remaining,
        HashSet<AssetPath> failed,
        HashSet<AssetPath> completed)
    {
        var succeeded = result.Status is OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings;
        var globalError = result.Diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
            && !inputs.Any(input => string.Equals(issue.AffectedVirtualPath, input.AssetUri.VirtualPath, StringComparison.Ordinal)));
        foreach (var input in inputs)
        {
            _ = remaining.Remove(input.AssetUri);
            var inputFailed = !succeeded
                && (globalError
                    || result.Diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                        && string.Equals(issue.AffectedVirtualPath, input.AssetUri.VirtualPath, StringComparison.Ordinal))
                    || !result.CookedAssets.Any(asset => asset.SourceAssetUri == input.AssetUri));
            _ = inputFailed ? failed.Add(input.AssetUri) : completed.Add(input.AssetUri);
        }
    }

    private async Task<List<ContentCookResult>> CookDependencyLayersAsync(
        ContentCookOperation operation,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        CookInputSnapshot snapshot,
        CookDependencyGraph graph,
        CookProvenance previous,
        CookIncrementalPlan plan,
        IReadOnlyList<ContentCookInput> dirtyInputs,
        CookStagingArea staging,
        IReadOnlyList<string> referenceRoots,
        ImportRunSettings run,
        CancellationToken cancellationToken)
    {
        var remaining = dirtyInputs.ToDictionary(static input => input.AssetUri);
        var failed = new HashSet<AssetPath>();
        var completed = plan.Reusable.Keys.ToHashSet();
        var results = new List<ContentCookResult>();
        while (remaining.Count > 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var blocked = remaining.Values.Where(input => SourceDependencies(input, graph).Any(failed.Contains)).ToArray();
            if (blocked.Length > 0)
            {
                results.Add(SkipDependentInputs(operation, targetKind, blocked, cycle: false));
                foreach (var input in blocked)
                {
                    _ = remaining.Remove(input.AssetUri);
                    _ = failed.Add(input.AssetUri);
                }

                continue;
            }

            var ready = ReadyInputs(remaining, graph);
            if (ready.Length == 0)
            {
                results.Add(SkipDependentInputs(operation, targetKind, [.. remaining.Values], cycle: true));
                break;
            }

            foreach (var mount in ready.GroupBy(static input => input.MountName, StringComparer.Ordinal))
            {
                var inputs = mount.ToArray();
                var scope = this.CreateScope(operation.Project, inputs, targetKind) with
                {
                    Snapshot = snapshot,
                    Artifacts = artifacts,
                    Importers = plan.Importers,
                    ReusableSources = [.. completed],
                    PreviousProvenance = previous,
                    PreviousInventories = plan.PriorInventories,
                    NativeJobs = graph.NativeJobs,
                    SceneDescriptors = graph.SceneDescriptors,
                    CapturedInputs = new([.. snapshot.CreateNativeInputs().Inputs, .. graph.GeneratedInputs]),
                    Output = staging.Roots.Single(root => string.Equals(root.Mount, mount.Key, StringComparison.Ordinal)),
                    CookedContextRoots = referenceRoots,
                    InputDependencies = graph.Dependencies,
                    Run = run,
                };
                var result = await this.CookMixedInputsAsync(operation.OperationId, scope, cancellationToken).ConfigureAwait(false);
                results.Add(result);
                RecordLayerOutcome(result, inputs, remaining, failed, completed);
            }
        }

        return results;
    }
}
