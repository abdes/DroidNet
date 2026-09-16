// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Import;

/// <summary>
/// Managed import/build primitive for supported non-scene assets.
/// </summary>
public interface IImportService
{
    /// <summary>
    /// Imports the requested inputs and produces runtime-ready artifacts (via Import → Build).
    /// </summary>
    /// <remarks>
    /// A batch containing scene assets returns a failed result with
    /// <c>OXYIMPORT_NATIVE_SCENE_COOK_REQUIRED</c> before cooking or repairing any cooked output.
    /// Extracted source data is retained. Production scene imports use the native content pipeline.
    /// </remarks>
    /// <param name="request">The import request.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The import result.</returns>
    public Task<ImportResult> ImportAsync(ImportRequest request, CancellationToken cancellationToken = default);
}
