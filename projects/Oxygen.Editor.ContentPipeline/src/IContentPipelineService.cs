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
    /// <summary>Retains a reviewed model source, creates settings, then cooks through the shared writer.</summary>
    /// <param name="request">The reviewed source and destination.</param>
    /// <param name="cancellationToken">Cancels the import and owned native work.</param>
    /// <returns>The cook result, including retained-source recovery when cooking fails.</returns>
    public Task<ContentCookResult> ImportSourceAsync(Import.SceneImportRequest request, CancellationToken cancellationToken);

    /// <summary>Reimports the latest saved retained source with its existing settings and identities.</summary>
    /// <param name="sourceUri">The retained source identity.</param>
    /// <param name="expectedProject">The project that originated the action.</param>
    /// <param name="cancellationToken">Cancels the selected request.</param>
    /// <returns>The shared incremental publication result.</returns>
    public Task<ContentCookResult> ReimportSourceAsync(Uri sourceUri, ProjectContext expectedProject, CancellationToken cancellationToken);

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
    /// <param name="validate">Whether to validate root integrity under the same read lease.</param>
    /// <param name="expectedProject">Optional originating project, preventing a delayed request from inspecting another activation.</param>
    /// <returns>The captured scope report.</returns>
    public Task<Inspection.CookedOutputReport> InspectCookedOutputAsync(Uri? scopeUri, CancellationToken cancellationToken, bool validate = false, ProjectContext? expectedProject = null);
}
