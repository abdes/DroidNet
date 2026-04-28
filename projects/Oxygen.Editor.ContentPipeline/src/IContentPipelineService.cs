// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Explicit editor content-pipeline workflow service.
/// </summary>
public interface IContentPipelineService
{
    /// <summary>
    /// Cooks the current scene and its ED-M07 dependencies.
    /// </summary>
    /// <param name="scene">The scene document.</param>
    /// <param name="sceneAssetUri">The scene asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookCurrentSceneAsync(
        Scene scene,
        Uri sceneAssetUri,
        CancellationToken cancellationToken);

    /// <summary>
    /// Cooks one asset.
    /// </summary>
    /// <param name="assetUri">The asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookAssetAsync(Uri assetUri, CancellationToken cancellationToken);

    /// <summary>
    /// Cooks all supported assets under a folder.
    /// </summary>
    /// <param name="folderUri">The folder URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookFolderAsync(Uri folderUri, CancellationToken cancellationToken);

    /// <summary>
    /// Cooks all supported assets in the active project.
    /// </summary>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookProjectAsync(CancellationToken cancellationToken);

    /// <summary>
    /// Inspects cooked output for the requested scope.
    /// </summary>
    /// <param name="scopeUri">The optional scope URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The inspection result.</returns>
    public Task<CookInspectionResult> InspectCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken);

    /// <summary>
    /// Validates cooked output for the requested scope.
    /// </summary>
    /// <param name="scopeUri">The optional scope URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The validation result.</returns>
    public Task<CookValidationResult> ValidateCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken);
}
