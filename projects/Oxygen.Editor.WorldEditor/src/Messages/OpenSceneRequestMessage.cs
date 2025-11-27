// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging.Messages;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
///     Message sent when a scene needs to be opened in the editor.
/// </summary>
/// <param name="scene">The <see cref="Scene"/> instance to be opened in the editor.</param>
public class OpenSceneRequestMessage(Scene scene) : RequestMessage<bool>
{
    /// <summary>
    ///     Gets the <see cref="Scene"/> instance to be opened in the editor.
    /// </summary>
    public Scene Scene { get; } = scene;
}
