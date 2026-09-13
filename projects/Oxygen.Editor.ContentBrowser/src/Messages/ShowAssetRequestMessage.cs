// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>Requests explicit navigation to an asset in the owning workspace's Content Browser.</summary>
/// <param name="project">The originating project activation.</param>
/// <param name="assetUri">The identity to reveal.</param>
public sealed class ShowAssetRequestMessage(ProjectContext project, Uri assetUri) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the originating project.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the requested identity.</summary>
    public Uri AssetUri { get; } = assetUri;
}
