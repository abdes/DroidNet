// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Microsoft.UI;

namespace Oxygen.Editor.World.Documents;

/// <summary>Requests recovery of the previous saved scene through the ordinary document lifetime owner.</summary>
/// <param name="windowId">The workspace window.</param>
internal sealed class ReloadPreviousSceneRequestMessage(WindowId windowId) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the owning workspace.</summary>
    public WindowId WindowId { get; } = windowId;
}
