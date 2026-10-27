// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// An authored scene a node path passes through, which <i>Apply to source</i> offers (scene instances 6.7): the
/// instance's source at level 0, the scene an instance root of that source instances at level 1, and so on.
/// </summary>
/// <param name="Scene">The authored scene.</param>
/// <param name="Level">How many nodes of the path lie above the scene: the instance's source is at 0.</param>
public sealed record ApplyTarget(AssetPath Scene, int Level)
{
    /// <summary>Gets the scene's name, such as <c>Watchtower</c>.</summary>
    public string Name => SceneContent.NameOf(this.Scene.Name);

    /// <summary>Gets the command's label, such as <c>Apply to Watchtower</c>.</summary>
    public string Label => $"Apply to {this.Name}";
}
