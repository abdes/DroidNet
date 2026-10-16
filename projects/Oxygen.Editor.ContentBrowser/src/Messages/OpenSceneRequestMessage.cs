// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
///     Message sent when a scene needs to be opened in the editor.
/// </summary>
/// <param name="sceneUri">The asset URI of the scene file to open.</param>
public class OpenSceneRequestMessage(Uri sceneUri) : AsyncRequestMessage<bool>
{
    /// <summary>
    ///     Gets the asset URI of the scene file to open.
    /// </summary>
    public Uri SceneUri { get; } = sceneUri;
}
