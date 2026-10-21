// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
/// Finds the open documents with unsaved changes that an explicit cook or Play reads, so they are saved before it runs.
/// A cook never waits for them: it reads the files as they are on disk.
/// </summary>
public interface IUnsavedCookInputs
{
    /// <summary>Finds the unsaved documents a cook reads: those of its assets and of the assets they use.</summary>
    /// <param name="request">The cook, by its target.</param>
    /// <param name="cancellationToken">Cancels reading the authored references.</param>
    /// <returns>The unsaved documents, sorted by title; empty when the cook reads none.</returns>
    public Task<IReadOnlyList<UnsavedDocument>> FindAsync(CookRunRequest request, CancellationToken cancellationToken);
}
