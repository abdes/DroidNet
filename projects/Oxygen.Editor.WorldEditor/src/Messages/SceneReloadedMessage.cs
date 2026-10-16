// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Messages;

/// <summary>Requests an active scene tree rebind after its authoring model was replaced.</summary>
/// <param name="scene">The accepted replacement model.</param>
/// <param name="document">The scene document whose content was replaced.</param>
/// <param name="windowId">The owning editor window.</param>
internal sealed class SceneReloadedMessage(Scene scene, Document document, WindowId windowId) : AsyncRequestMessage<bool>
{
    /// <summary>Gets the replacement model.</summary>
    public Scene Scene { get; } = scene;

    /// <summary>Gets the scene document whose content was replaced.</summary>
    public Document Document { get; } = document;

    /// <summary>Gets the editor window.</summary>
    public WindowId WindowId { get; } = windowId;
}
