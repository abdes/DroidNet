// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Routes saved-source and preview demand through the same project-owned transaction.</summary>
public sealed partial class ContentPipelineService
{
    /// <inheritdoc />
    public Task<ContentCookResult> CookSavedAssetAsync(Uri assetUri, ProjectContext expectedProject, CancellationToken cancellationToken)
        => this.CookAutomaticAssetAsync(assetUri, expectedProject, isDemand: false, cancellationToken);

    /// <inheritdoc />
    public Task<ContentCookResult> CookPreviewAssetAsync(Uri assetUri, ProjectContext expectedProject, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        return GetAssetKind(assetUri) is ContentCookAssetKind.Geometry or ContentCookAssetKind.Material
            ? this.CookAutomaticAssetAsync(assetUri, expectedProject, isDemand: true, cancellationToken)
            : throw new ArgumentException("Preview demand requires an authored geometry or material asset.", nameof(assetUri));
    }

    private Task<ContentCookResult> CookAutomaticAssetAsync(Uri assetUri, ProjectContext expectedProject, bool isDemand, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        ArgumentNullException.ThrowIfNull(expectedProject);
        return this.cookCoordinator.RunCookAsync(
            new(CookTargetKind.Asset, assetUri, IsAutomatic: true)
            {
                CoalescePending = true,
                IsDemand = isDemand,
                OriginContext = expectedProject,
            },
            (operation, token) => ReferenceEquals(operation.Project, expectedProject)
                ? this.CookAssetCoreAsync(operation, assetUri, token, allowImportedSourceChanges: false)
                : throw new OperationCanceledException("The saved source's project is no longer active.", token),
            cancellationToken);
    }
}
