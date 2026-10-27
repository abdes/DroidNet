// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>
/// The editing state of scene nodes that is not part of the scene: nodes hidden in the editor and nodes locked against
/// editing. Commands obey it; <see cref="Interaction.SceneNodeInteraction"/> keeps and persists it per project.
/// </summary>
public interface ISceneNodeInteraction
{
    /// <summary>Gets whether a node is hidden in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is hidden.</returns>
    public bool IsHidden(NodeReference node);

    /// <summary>Gets whether a node is locked against editing.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is locked.</returns>
    public bool IsLocked(NodeReference node);

    /// <summary>Finds the closest locked node that protects a node from editing.</summary>
    /// <param name="node">The scene node.</param>
    /// <returns>The node itself or an ancestor that is locked; otherwise, <see langword="null"/>.</returns>
    public SceneNode? GetLockOwner(SceneNode node);

    /// <summary>Gets a node's ancestors, nearest first, which hiding and locking follow down to it.</summary>
    /// <param name="scene">The scene the node is in.</param>
    /// <param name="node">The node, which an instance may have brought in.</param>
    /// <returns>
    /// The ancestors in its instance's contents, its instance root and the root's ancestors; <see langword="null"/> for a
    /// node an instance brings in whose instance's contents are not read yet.
    /// </returns>
    public IReadOnlyList<NodeReference>? GetAncestors(Scene scene, NodeReference node);

    /// <summary>Finds what protects a node, which an instance may have brought in, from editing.</summary>
    /// <param name="scene">The scene the node is in.</param>
    /// <param name="node">The node.</param>
    /// <returns>
    /// The closest locked node among the node, its ancestors in its instance's contents, its instance root and the
    /// root's ancestors; for a node an instance brings in whose instance's contents are not read yet, that instance.
    /// </returns>
    public NodeLock GetLock(Scene scene, NodeReference node);

    /// <summary>Gets the nodes locked themselves in the active scene.</summary>
    /// <returns>A snapshot of the locked nodes, in no particular order.</returns>
    public IReadOnlyCollection<NodeReference> LockedNodes();

    /// <summary>Hides or shows a node in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(NodeReference node, bool isHidden);
}
