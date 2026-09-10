// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>Identifies a scene whose authoring state changed before live synchronization.</summary>
/// <param name="scene">The modified scene.</param>
public sealed class SceneAuthoringChangedEventArgs(Scene scene) : EventArgs
{
    /// <summary>Gets the modified scene.</summary>
    public Scene Scene { get; } = scene;
}
