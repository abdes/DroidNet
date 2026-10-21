// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// The project session's clipboard of scene nodes: the one payload that a copy or cut stages and a paste reads,
/// whichever window or panel of the session does either.
/// </summary>
/// <remarks>
/// A copy can be pasted into any scene of the session; a cut only into the scene that staged it, while that scene is
/// loaded. The clipboard ends with the session, so nothing staged in one project is ever pasted into another, and it
/// survives a change of the project's content mounts.
/// </remarks>
public interface ISceneNodeClipboard
{
    /// <summary>Gets the staged payload, or <see langword="null"/> when the clipboard is empty.</summary>
    public SceneClipboard? Payload { get; }

    /// <summary>Gets a value indicating whether the payload was cut rather than copied.</summary>
    public bool IsCut { get; }

    /// <summary>
    /// Gets the clipboard's generation, which changes with every stage and clear: a paste that completes a cut compares
    /// it to know that the cut it moved is still the staged one.
    /// </summary>
    public int Generation { get; }

    /// <summary>Stages a payload, replacing the one staged before.</summary>
    /// <param name="payload">The copied or cut nodes and folders.</param>
    /// <param name="cut">Whether the items are cut, so a paste moves them.</param>
    /// <param name="origin">The scene the items come from.</param>
    public void Stage(SceneClipboard payload, bool cut, Scene origin);

    /// <summary>Says whether the staged payload may be pasted into a scene.</summary>
    /// <param name="scene">The scene the paste targets, if any.</param>
    /// <returns>
    /// <see langword="true"/> for a copy, or for a cut when <paramref name="scene"/> is the scene that staged it;
    /// <see langword="true"/> as well when nothing is staged, since there is nothing to discard.
    /// </returns>
    public bool IsCurrentFor(Scene? scene);

    /// <summary>Empties the clipboard.</summary>
    public void Clear();
}
