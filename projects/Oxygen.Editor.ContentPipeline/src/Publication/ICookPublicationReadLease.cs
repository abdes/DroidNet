// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A read lease on one cook publication, which keeps its cooked output stable while held.</summary>
/// <remarks>
///     Leases are issued by <see cref="ICookPublicationReader"/>. Content pipeline operations that take a
///     lease back accept only leases the pipeline issued.
/// </remarks>
public interface ICookPublicationReadLease : IDisposable
{
    /// <summary>Gets the selected publication identity, or <see langword="null"/> before the first publication.</summary>
    public Guid? PublicationId { get; }

    /// <summary>Gets the identity of the project the publication belongs to.</summary>
    public Guid ProjectId { get; }

    /// <summary>Gets the root folder of the project the publication belongs to.</summary>
    public string ProjectRoot { get; }

    /// <summary>Gets the accepted roots in native precedence order.</summary>
    public ImmutableArray<CookPublicationRoot> Roots { get; }

    /// <summary>Gets owned roots whose lifetime marker is missing; they must be rebuilt before they are read.</summary>
    public ImmutableArray<CookPublicationRoot> UnavailableRoots { get; }

    /// <summary>Gets the physical output folder of each project mount, by mount name.</summary>
    public ImmutableDictionary<string, string> ProjectRoots { get; }

    /// <summary>Opens the cooked index of every available root as a catalog tied to this publication.</summary>
    /// <param name="cancellationToken">Cancels reading the indexes.</param>
    /// <returns>One catalog per available root, in native precedence order; the caller owns and disposes them.</returns>
    /// <exception cref="CookedLibraryChangedException">A library's index was replaced after this publication was captured.</exception>
    /// <exception cref="InvalidDataException">The project's own cooked output no longer matches this publication.</exception>
    public Task<IReadOnlyList<IAssetCatalog>> CreateCatalogsAsync(CancellationToken cancellationToken);

    /// <summary>Takes another hold on the same publication, released independently by disposing it.</summary>
    /// <returns>A lease on the same publication.</returns>
    public ICookPublicationReadLease Retain();
}
