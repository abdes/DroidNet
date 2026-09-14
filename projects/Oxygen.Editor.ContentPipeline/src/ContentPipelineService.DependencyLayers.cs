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

/// <summary>Orders source work across output mounts while allowing independent branches to finish.</summary>
public sealed partial class ContentPipelineService
{
    private static ImmutableArray<Uri> SourceDependencies(ContentCookInput input, CookDependencyGraph graph)
        => graph.Dependencies.TryGetValue(input.AssetUri, out var dependencies) ? dependencies
            : ProceduralGeometryDescriptorService.IsGeneratedBasicShape(input.AssetUri) ? [AssetUris.BuildGeneratedUri("Materials/Default")] : [];

    private static ContentCookInput[] ReadyInputs(Dictionary<Uri, ContentCookInput> remaining, CookDependencyGraph graph)
    {
        var ready = remaining.Values.Where(input => SourceDependencies(input, graph).All(uri => !remaining.ContainsKey(uri))).Select(static input => input.AssetUri).ToHashSet();
        bool expanded;
        do
        {
            expanded = false;
            foreach (var input in remaining.Values.Where(input => !ready.Contains(input.AssetUri)))
            {
                if (SourceDependencies(input, graph).All(uri => !remaining.TryGetValue(uri, out var dependency)
                    || (ready.Contains(uri) && string.Equals(dependency.MountName, input.MountName, StringComparison.OrdinalIgnoreCase))))
                {
                    _ = ready.Add(input.AssetUri);
                    expanded = true;
                }
            }
        }
        while (expanded);

        return remaining.Values.Where(input => ready.Contains(input.AssetUri)).ToArray();
    }

    private static ContentCookResult SkipDependentInputs(ContentCookOperation operation, CookTargetKind targetKind, IReadOnlyList<ContentCookInput> inputs, bool cycle)
    {
        var diagnostics = inputs.Select(input => new DiagnosticRecord
        {
            OperationId = operation.OperationId,
            Domain = FailureDomain.AssetCook,
            Severity = cycle ? DiagnosticSeverity.Error : DiagnosticSeverity.Warning,
            Code = cycle ? "asset_cook.dependency_cycle" : "asset_cook.dependency_failed",
            AffectedVirtualPath = input.AssetUri.AbsolutePath,
            Message = cycle ? $"A dependency cycle prevents cooking '{input.AssetUri}'." : $"Skipped '{input.AssetUri}' because a required asset failed.",
        }).ToArray();
        foreach (var input in inputs)
        {
            CookRunContext.Report(new(Asset: new(input.AssetUri, input.Kind, CookAssetState.Skipped)));
        }

        return new(operation.OperationId, targetKind, OperationStatus.Failed, diagnostics, [], Inspection: null, Validation: null);
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
        CancellationToken cancellationToken)
    {
        var remaining = dirtyInputs.ToDictionary(static input => input.AssetUri);
        var failed = new HashSet<Uri>();
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
                results.Add(SkipDependentInputs(operation, targetKind, remaining.Values.ToArray(), cycle: true));
                break;
            }

            foreach (var mount in ready.GroupBy(static input => input.MountName, StringComparer.OrdinalIgnoreCase))
            {
                var inputs = mount.Select(input => input with { SourceAbsolutePath = Path.Combine(snapshot.InputRoot, input.SourceRelativePath) }).ToArray();
                var scope = this.CreateScope(operation.Project, inputs, targetKind) with
                {
                    Snapshot = snapshot, Artifacts = artifacts, ReusableSources = completed.ToImmutableHashSet(), PreviousProvenance = previous,
                    StagingOutputRoot = staging.Roots.Single(root => string.Equals(root.Mount, mount.Key, StringComparison.OrdinalIgnoreCase)).StagingPath,
                    CookedContextRoots = referenceRoots,
                };
                var result = await this.CookMixedInputsAsync(operation.OperationId, scope, cancellationToken).ConfigureAwait(false);
                results.Add(result);
                var globalError = result.Diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                    && !mount.Any(input => string.Equals(issue.AffectedVirtualPath, input.AssetUri.AbsolutePath, StringComparison.Ordinal)));
                foreach (var input in mount)
                {
                    _ = remaining.Remove(input.AssetUri);
                    if (result.Status is not (OperationStatus.Succeeded or OperationStatus.SucceededWithWarnings)
                        && (globalError || result.Diagnostics.Any(issue => issue.Severity >= DiagnosticSeverity.Error
                            && string.Equals(issue.AffectedVirtualPath, input.AssetUri.AbsolutePath, StringComparison.Ordinal))
                            || !result.CookedAssets.Any(asset => asset.SourceAssetUri == input.AssetUri)))
                    {
                        _ = failed.Add(input.AssetUri);
                    }
                    else
                    {
                        _ = completed.Add(input.AssetUri);
                    }
                }
            }
        }

        return results;
    }
}
