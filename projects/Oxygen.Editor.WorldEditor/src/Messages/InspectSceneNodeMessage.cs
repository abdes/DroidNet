// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Microsoft.UI;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Messages;

/// <summary>Requests that Scene Explorer reveal and select a scene node for inspection.</summary>
/// <param name="NodeId">The node identity to reveal and select.</param>
/// <param name="WindowId">The editor window owning the request.</param>
internal sealed class InspectSceneNodeMessage(NodeId NodeId, WindowId WindowId) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the scene-node identity to inspect.</summary>
    public NodeId NodeId { get; } = NodeId;

    /// <summary>Gets the editor window owning the request.</summary>
    public WindowId WindowId { get; } = WindowId;
}
