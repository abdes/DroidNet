// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Requests explicit navigation to assets in the owning workspace's Content Browser.</summary>
/// <param name="project">The originating project activation.</param>
/// <param name="assetUris">The identities to reveal and select together; never empty.</param>
public sealed class ShowAssetRequestMessage(ProjectContext project, IReadOnlyList<Uri> assetUris) : AsyncRequestMessage<bool>
{
    /// <summary>Initializes a new instance of the <see cref="ShowAssetRequestMessage"/> class for one asset.</summary>
    /// <param name="project">The originating project activation.</param>
    /// <param name="assetUri">The identity to reveal.</param>
    public ShowAssetRequestMessage(ProjectContext project, Uri assetUri)
        : this(project, [assetUri])
    {
    }

    /// <summary>Gets the originating project.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the requested identities.</summary>
    public IReadOnlyList<Uri> AssetUris { get; } = assetUris.Count > 0
        ? assetUris
        : throw new ArgumentException("At least one asset must be requested.", nameof(assetUris));
}
