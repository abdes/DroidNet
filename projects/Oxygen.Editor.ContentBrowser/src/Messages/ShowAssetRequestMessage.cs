// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Requests explicit navigation to assets in the owning workspace's Content Browser.</summary>
/// <param name="project">The originating project activation.</param>
/// <param name="assets">The identities to reveal and select together; never empty.</param>
public sealed class ShowAssetRequestMessage(ProjectContext project, IReadOnlyList<AssetPath> assets) : AsyncRequestMessage<bool>
{
    /// <summary>Initializes a new instance of the <see cref="ShowAssetRequestMessage"/> class for one asset.</summary>
    /// <param name="project">The originating project activation.</param>
    /// <param name="asset">The identity to reveal.</param>
    public ShowAssetRequestMessage(ProjectContext project, AssetPath asset)
        : this(project, [asset])
    {
    }

    /// <summary>Gets the originating project.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the requested identities.</summary>
    public IReadOnlyList<AssetPath> Assets { get; } = assets.Count > 0
        ? assets
        : throw new ArgumentException("At least one asset must be requested.", nameof(assets));
}
