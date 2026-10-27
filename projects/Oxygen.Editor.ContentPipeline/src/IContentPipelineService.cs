// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

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

    /// <summary>Imports the models of one import dialog as one run: the new models are retained, get their records and
    /// cook as one native batch; each replacement then runs on its own, as its capture and commit belong to its model.
    /// Every cook runs with <paramref name="run"/>.</summary>
    /// <param name="requests">The reviewed models, all of one project, in the dialog's order.</param>
    /// <param name="run">The ImportTool's execution settings of the run.</param>
    /// <param name="cancellationToken">Cancels the imports and owned native work.</param>
    /// <returns>The cook results: the new models' first, then each replacement's.</returns>
    public Task<IReadOnlyList<ContentCookResult>> ImportSourcesAsync(IReadOnlyList<Import.SceneImportRequest> requests, Oxygen.Editor.Models.ImportRunSettings run, CancellationToken cancellationToken);

    /// <summary>Reimports the latest saved retained source with its existing settings and identities.</summary>
    /// <param name="sourceUri">The retained source identity.</param>
    /// <param name="expectedProject">The project that originated the action.</param>
    /// <param name="cancellationToken">Cancels the selected request.</param>
    /// <returns>The shared incremental publication result.</returns>
    public Task<ContentCookResult> ReimportSourceAsync(AssetPath sourceUri, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>
    /// Replaces a model's import record, when it still holds the expected bytes, and reimports the model, as one operation
    /// of the project's writer. A record that changed since is left as it is, and the operation fails.
    /// </summary>
    /// <param name="sourceUri">The model's source.</param>
    /// <param name="expected">The bytes the record holds now.</param>
    /// <param name="replacement">The record's new bytes.</param>
    /// <param name="expectedProject">The project that originated the action.</param>
    /// <param name="cancellationToken">Cancels the request.</param>
    /// <returns>The reimport's result.</returns>
    public Task<ContentCookResult> ReplaceModelRecordAsync(
        AssetPath sourceUri,
        ReadOnlyMemory<byte> expected,
        ReadOnlyMemory<byte> replacement,
        ProjectContext expectedProject,
        CancellationToken cancellationToken);

    /// <summary>Cooks saved content needed by an active preview, ahead of queued background saves.</summary>
    /// <param name="assetUri">The required authored geometry or material identity.</param>
    /// <param name="expectedProject">The project that owns the preview request.</param>
    /// <param name="cancellationToken">Detaches this preview observer without cancelling other callers sharing the cook.</param>
    /// <returns>The shared pipeline's incremental result.</returns>
    public Task<ContentCookResult> CookPreviewAssetAsync(AssetPath assetUri, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>Cooks an acknowledged saved asset as background work in its originating project.</summary>
    /// <param name="assetUri">The saved source identity.</param>
    /// <param name="expectedProject">The project that owned the Save.</param>
    /// <param name="cancellationToken">Cancels this request.</param>
    /// <returns>The shared pipeline's incremental result.</returns>
    public Task<ContentCookResult> CookSavedAssetAsync(AssetPath assetUri, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>Cooks changed saved sources together, as one background run of their originating project.</summary>
    /// <remarks>
    /// One source is cooked as that asset's run, several as one <see cref="CookTargetKind.Changes"/> run. The run names
    /// each skipped source in an Info diagnostic. A source moved or deleted before the run starts leaves it; the run
    /// removes the output it published.
    /// </remarks>
    /// <param name="changes">The changed sources, and those left out for their errors.</param>
    /// <param name="expectedProject">The project whose files changed.</param>
    /// <param name="cancellationToken">Cancels this request.</param>
    /// <returns>The shared pipeline's incremental result.</returns>
    /// <exception cref="ArgumentException"><paramref name="changes"/> has no source to cook.</exception>
    public Task<ContentCookResult> CookChangedSourcesAsync(Cooking.CookChanges changes, ProjectContext expectedProject, CancellationToken cancellationToken);

    /// <summary>
    /// Cooks the current scene's saved source and required dependencies.
    /// </summary>
    /// <param name="sceneAssetUri">The scene asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookCurrentSceneAsync(
        AssetPath sceneAssetUri,
        CancellationToken cancellationToken);

    /// <summary>
    /// Cooks one asset.
    /// </summary>
    /// <param name="assetUri">The asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <param name="expectedProject">Optional originating project facts, checked again inside the writer.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookAssetAsync(AssetPath assetUri, CancellationToken cancellationToken, ProjectContext? expectedProject = null);

    /// <summary>Cooks several assets of the active project together, as one <see cref="CookTargetKind.Changes"/> run.</summary>
    /// <param name="assetUris">The assets, such as those of a cook on change run to cook again.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    /// <exception cref="ArgumentException"><paramref name="assetUris"/> is empty.</exception>
    public Task<ContentCookResult> CookAssetsAsync(IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken);

    /// <summary>
    /// Cooks all supported assets under a folder.
    /// </summary>
    /// <param name="folderUri">The folder URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<ContentCookResult> CookFolderAsync(AssetPath folderUri, CancellationToken cancellationToken);

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
    public Task<Inspection.CookedOutputReport> InspectCookedOutputAsync(AssetPath? scopeUri, CancellationToken cancellationToken, bool validate = false, ProjectContext? expectedProject = null);
}
