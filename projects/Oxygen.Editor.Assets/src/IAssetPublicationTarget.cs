// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.Assets;

/// <summary>
/// Receives the cook publication the editor accepted, so that the asset catalog shows that publication's cooked
/// content.
/// </summary>
/// <remarks>
/// The owners of publication acceptance (mounting cooked content, changing mounts, committing a cook to the preview)
/// call this after they accept a publication. The catalog never republishes a publication it receives this way.
/// </remarks>
public interface IAssetPublicationTarget
{
    /// <summary>Shows the cooked content of the given publication.</summary>
    /// <param name="publication">A borrowed lease on the accepted publication; the catalog takes its own hold.</param>
    /// <param name="cancellationToken">Cancels reading the publication's cooked indexes.</param>
    /// <returns>Completion after the catalog shows the publication.</returns>
    public Task UsePublicationAsync(ICookPublicationReadLease publication, CancellationToken cancellationToken = default);
}
