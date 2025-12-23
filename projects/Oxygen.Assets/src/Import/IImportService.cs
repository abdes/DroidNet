// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Entry point for editor-driven import operations.
/// </summary>
public interface IImportService
{
    /// <summary>
    /// Imports the requested inputs and produces runtime-ready artifacts (via Import → Build).
    /// </summary>
    /// <param name="request">The import request.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The import result.</returns>
    public Task<ImportResult> ImportAsync(ImportRequest request, CancellationToken cancellationToken = default);
}
