// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Narrow adapter over native engine content-pipeline capabilities.
/// </summary>
public interface IEngineContentPipelineApi
{
    /// <summary>
    /// Imports using a native manifest or a bounded ImportTool fallback.
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
