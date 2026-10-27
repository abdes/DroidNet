// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The direct references of the project's saved authored assets: the edges the cook dependency graph follows, such as
/// a scene's instanced sources, as the saved descriptors hold them when last read.
/// </summary>
public interface IAuthoredReferences
{
    /// <summary>Gets the assets a saved authored asset references directly, as last read.</summary>
    /// <param name="asset">The asset's identity, such as <c>/Content/Watchtower.oscene</c>.</param>
    /// <returns>The referenced identities; none for an asset that has no saved descriptor or was never read.</returns>
    public IReadOnlyList<AssetPath> ReferencesOf(AssetPath asset);

    /// <summary>Reads the saved descriptors of the project again.</summary>
    /// <param name="cancellationToken">Cancels the read; the references read before stay.</param>
    /// <returns>The asynchronous read.</returns>
    public Task RefreshAsync(CancellationToken cancellationToken);
}
