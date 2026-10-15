// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Gives readers access to a project's selected cook publication.</summary>
/// <remarks>
///     The publication format is private to the content pipeline; readers depend on this contract and on
///     the leases it issues.
/// </remarks>
public interface ICookPublicationReader
{
    /// <summary>Acquires the project's selected publication, recovering an interrupted one first.</summary>
    /// <param name="project">The project whose publication is read.</param>
    /// <param name="cancellationToken">Cancels the acquisition.</param>
    /// <returns>A lease on the selected publication, released by disposing it.</returns>
    public Task<ICookPublicationReadLease> AcquireReadAsync(ProjectContext project, CancellationToken cancellationToken);

    /// <summary>
    ///     Acquires the project's selected publication with the catalogs of its cooked roots. When a library's cooked
    ///     index changed after the publication was made, the project is published again from its current libraries
    ///     and that publication is returned instead.
    /// </summary>
    /// <param name="project">The project whose publication is read.</param>
    /// <param name="cancellationToken">Cancels the acquisition.</param>
    /// <returns>The publication and its catalogs, which the caller owns and disposes.</returns>
    public Task<CookPublicationCatalogs> AcquireCatalogsAsync(ProjectContext project, CancellationToken cancellationToken);
}
