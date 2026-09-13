// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Explicit editor content-pipeline workflow service.
/// </summary>
public interface IContentPipelineService : Status.IAssetCookStatusReader
{
    /// <summary>Cooks saved content needed by an active preview, ahead of queued background saves.</summary>
    /// <param name="assetUri">The required authored geometry or material identity.</param>
    /// <param name="expectedProject">The project that owns the preview request.</param>
    /// <param name="cancellationToken">Detaches this preview observer without cancelling other callers sharing the cook.</param>
    /// <returns>The shared pipeline's incremental result.</returns>
    public Task<ContentCookResult> CookPreviewAssetAsync(Uri assetUri, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>Cooks an acknowledged saved asset as background work in its originating project.</summary>
    /// <param name="assetUri">The saved source identity.</param>
    /// <param name="expectedProject">The project that owned the Save.</param>
    /// <param name="cancellationToken">Cancels this request.</param>
    /// <returns>The shared pipeline's incremental result.</returns>
    public Task<ContentCookResult> CookSavedAssetAsync(Uri assetUri, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>
    /// Cooks the current scene's saved source and required dependencies.
    /// </summary>
    /// <param name="sceneAssetUri">The scene asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookCurrentSceneAsync(
        Uri sceneAssetUri,
        CancellationToken cancellationToken);

    /// <summary>
    /// Cooks one asset.
    /// </summary>
    /// <param name="assetUri">The asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <param name="expectedProject">Optional originating project facts, checked again inside the writer.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookAssetAsync(Uri assetUri, CancellationToken cancellationToken, ProjectContext? expectedProject = null);

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
