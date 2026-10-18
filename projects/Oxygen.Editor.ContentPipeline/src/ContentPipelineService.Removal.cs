// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Removes the outputs of retired products from inherited roots before anything is cooked into them.</summary>
public sealed partial class ContentPipelineService
{
    /// <summary>Removes each staged root's retired outputs and verifies the root that is left.</summary>
    /// <returns>One verified result per root with removals, in staging order, ahead of the cook results for the same roots.</returns>
    private async Task<List<ContentCookResult>> RemoveRetiredOutputsAsync(
        ContentCookOperation operation,
        CookTargetKind targetKind,
        NativeArtifactLease artifacts,
        CookStagingArea staging,
        CookIncrementalPlan plan,
        CancellationToken cancellationToken)
    {
        var results = new List<ContentCookResult>();
        var operationRoot = Path.Combine(operation.Project.ProjectRoot.FullPath, ".build", "cook", operation.OperationId.ToString("N"));
        foreach (var root in staging.Roots)
        {
            if (!plan.Removals.TryGetValue(root.Mount, out var removal))
            {
                continue;
            }

            CookRunContext.Report(new(Message: $"Removing cooked output from {root.Mount}: {string.Join(", ", removal.VirtualPaths.Concat(removal.AuxiliaryFiles))}.", State: CookRunState.Cooking));
            await root.PrepareWriteAsync().ConfigureAwait(false);
            await this.engineContentPipelineApi.RemoveCookedOutputsAsync(operationRoot, root.Path, root.SourceKey, removal, artifacts, cancellationToken).ConfigureAwait(false);
            results.Add(await this.VerifyRemovalAsync(operation.OperationId, targetKind, artifacts, root, cancellationToken).ConfigureAwait(false));
        }

        return results;
    }

    private async Task<ContentCookResult> VerifyRemovalAsync(Guid operationId, CookTargetKind targetKind, NativeArtifactLease artifacts, CookStagingRoot root, CancellationToken cancellationToken)
    {
        var lease = await CookOutputReadLease.AcquireAsync(root.Path, cancellationToken).ConfigureAwait(false);
        try
        {
            var inventory = await lease.ReadInventoryAsync(this.engineContentPipelineApi, cancellationToken, artifacts).ConfigureAwait(false);
            var validation = inventory.ToValidation(root.Path);
            if (!inventory.IsValid)
            {
                return new(operationId, targetKind, OperationStatus.Failed, NormalizeDiagnostics(operationId, validation.Diagnostics), CookedAssets: [], inventory.ToInspection(root.Path), validation);
            }

            root.AcceptVerification(lease, inventory);
            lease = null;
            return new(operationId, targetKind, OperationStatus.Succeeded, [], CookedAssets: [], inventory.ToInspection(root.Path), validation)
            {
                VerifiedRoot = new CookProvenance.Root(root.Mount, inventory.SourceKey, inventory.IndexSha256),
                NativeInventory = inventory,
            };
        }
        finally
        {
            if (lease is not null)
            {
                await lease.DisposeAsync().ConfigureAwait(false);
            }
        }
    }
}
