// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message indicating that a scene has been fully created/synchronized in the engine.
/// </summary>
/// <param name="scene">The <see cref="Scene"/> that was loaded into the engine.</param>
internal sealed class SceneLoadedMessage(Scene scene)
{
    /// <summary>
    /// Gets the loaded <see cref="Scene"/>.
    /// </summary>
    public Scene Scene { get; } = scene;
}
