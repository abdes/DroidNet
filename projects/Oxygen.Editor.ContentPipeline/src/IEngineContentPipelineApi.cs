// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Narrow adapter over native engine content-pipeline capabilities.
/// </summary>
public interface IEngineContentPipelineApi
{
    /// <summary>
    /// Imports using a native manifest or a bounded ImportTool fallback.
    /// </summary>
    /// <param name="manifest">The content import manifest.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The native import result.</returns>
    public Task<NativeImportResult> ImportAsync(
        ContentImportManifest manifest,
        CancellationToken cancellationToken);

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
