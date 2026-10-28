// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Native source analysis, cooking and inventory operations.
/// </summary>
public interface IEngineContentPipelineApi
{
    /// <summary>Analyzes one dependency frontier without creating cooked output.</summary>
    /// <param name="execution">Logical sources, native recipes and owned query paths.</param>
    /// <param name="cancellationToken">Cancels the native worker and waits for its ownership drain.</param>
    /// <returns>Native source facts bound to the verified producer.</returns>
    public Task<NativeSourceAnalysisReport> AnalyzeSourcesAsync(
        ContentSourceAnalysisExecution execution, CancellationToken cancellationToken);

    /// <summary>
    /// Executes a native import manifest.
    /// </summary>
    /// <param name="execution">The manifest and explicit physical execution paths.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The native import result.</returns>
    public Task<NativeImportResult> ImportAsync(
        ContentImportExecution execution,
        CancellationToken cancellationToken);

    /// <summary>Verifies all native inventory members while the caller holds its output read lease.</summary>
    /// <param name="cookedRoot">The protected physical root.</param>
    /// <param name="artifacts">Optional native artifact lease already owned by this operation.</param>
    /// <param name="cancellationToken">Cancels the native worker and waits for its ownership drain.</param>
    /// <returns>Expected inventory metadata and observed per-file failures.</returns>
    public Task<CookedInventoryReport> ReadInventoryAsync(string cookedRoot, NativeArtifactLease? artifacts, CancellationToken cancellationToken);

    /// <summary>Reads the revision of each importer of the ImportTool that cooks, through its <c>versions</c>.</summary>
    /// <param name="artifacts">Optional native artifact lease already owned by this operation.</param>
    /// <param name="cancellationToken">Cancels the native worker and waits for its ownership drain.</param>
    /// <returns>The importers' revisions, which every product's fingerprint holds.</returns>
    public Task<Incremental.ImporterRevisions> ReadImporterRevisionsAsync(NativeArtifactLease? artifacts, CancellationToken cancellationToken);

    /// <summary>Removes cooked assets and auxiliary files from a loose cooked root the caller owns, without cooking.</summary>
    /// <remarks>
    /// The index is updated first and the files are deleted after it, so the index never lists a deleted file.
    /// Resource table entries the removed assets used stay in their tables.
    /// </remarks>
    /// <param name="operationRoot">The operation's private working folder.</param>
    /// <param name="cookedRoot">The writable physical root.</param>
    /// <param name="sourceKey">The identity the updated index takes, as a cook into the root gives it.</param>
    /// <param name="removal">The assets and auxiliary files to remove; each must be in the root's index.</param>
    /// <param name="artifacts">The native artifact lease owned by this operation.</param>
    /// <param name="cancellationToken">Cancels the native worker and waits for its ownership drain.</param>
    /// <returns>A task that completes when the root no longer has the removed outputs.</returns>
    public Task RemoveCookedOutputsAsync(string operationRoot, string cookedRoot, Guid sourceKey, CookedOutputRemoval removal, NativeArtifactLease artifacts, CancellationToken cancellationToken);

    /// <summary>
    /// Inspects a loose cooked root.
    /// </summary>
    /// <param name="cookedRoot">The cooked root path.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The inspection result.</returns>
    public Task<CookInspectionResult> InspectLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken);

    /// <summary>
    /// Validates a loose cooked root.
    /// </summary>
    /// <param name="cookedRoot">The cooked root path.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The validation result.</returns>
    public Task<CookValidationResult> ValidateLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken);
}
