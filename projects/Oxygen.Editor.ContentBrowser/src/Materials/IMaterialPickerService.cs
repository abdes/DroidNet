// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Queries and resolves material picker rows for material assignment surfaces.
/// </summary>
public interface IMaterialPickerService
{
    /// <summary>
    /// Gets the latest material picker result snapshot.
    /// </summary>
    IObservable<IReadOnlyList<MaterialPickerResult>> Results { get; }

    /// <summary>
    /// Refreshes material picker results for a filter.
    /// </summary>
    /// <param name="filter">The picker filter.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The refresh task.</returns>
    Task RefreshAsync(MaterialPickerFilter filter, CancellationToken cancellationToken = default);

    /// <summary>
    /// Resolves one material assignment row, including missing assignments.
    /// </summary>
    /// <param name="materialUri">The material assignment URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The material picker row, or <see langword="null"/> when the URI is invalid.</returns>
    Task<MaterialPickerResult?> ResolveAsync(Uri materialUri, CancellationToken cancellationToken = default);
}
