// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
///     Message sent when a scene needs to be opened in the editor.
/// </summary>
/// <param name="scene">The scene to open.</param>
public class OpenSceneRequestMessage(AssetPath scene) : AsyncRequestMessage<bool>
{
    /// <summary>
    ///     Gets the scene to open.
    /// </summary>
    public AssetPath Scene { get; } = scene;
}
