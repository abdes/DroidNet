// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>The editor's hidden and locked nodes as a scratch validation sees them: hiding changes only the scratch copy.</summary>
/// <param name="live">The workspace's interaction state, which the scratch reads and never writes.</param>
internal sealed class ScratchNodeInteraction(ISceneNodeInteraction live) : ISceneNodeInteraction
{
    private readonly Dictionary<NodeReference, bool> hidden = [];

    /// <inheritdoc/>
    public bool IsHidden(NodeReference node) => this.hidden.TryGetValue(node, out var isHidden) ? isHidden : live.IsHidden(node);

    /// <inheritdoc/>
    public bool IsLocked(NodeReference node) => live.IsLocked(node);

    /// <inheritdoc/>
    public SceneNode? GetLockOwner(SceneNode node) => live.GetLockOwner(node);

    /// <inheritdoc/>
    public IReadOnlyList<NodeReference>? GetAncestors(Scene scene, NodeReference node) => live.GetAncestors(scene, node);

    /// <inheritdoc/>
    public NodeLock GetLock(Scene scene, NodeReference node) => live.GetLock(scene, node);

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> LockedNodes() => live.LockedNodes();

    /// <inheritdoc/>
    public void SetHidden(NodeReference node, bool isHidden) => this.hidden[node] = isHidden;
}
