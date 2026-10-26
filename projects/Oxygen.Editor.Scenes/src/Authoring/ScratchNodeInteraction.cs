// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>The editor's hidden and locked nodes as a scratch validation sees them: hiding changes only the scratch copy.</summary>
/// <param name="live">The workspace's interaction state, which the scratch reads and never writes.</param>
internal sealed class ScratchNodeInteraction(ISceneNodeInteraction live) : ISceneNodeInteraction
{
    private readonly Dictionary<NodeId, bool> hidden = [];

    /// <inheritdoc/>
    public bool IsHidden(NodeId nodeId) => this.hidden.TryGetValue(nodeId, out var isHidden) ? isHidden : live.IsHidden(nodeId);

    /// <inheritdoc/>
    public bool IsLocked(NodeReference node) => live.IsLocked(node);

    /// <inheritdoc/>
    public SceneNode? GetLockOwner(SceneNode node) => live.GetLockOwner(node);

    /// <inheritdoc/>
    public NodeReference? GetLockOwner(Scene scene, NodeReference node) => live.GetLockOwner(scene, node);

    /// <inheritdoc/>
    public void SetHidden(NodeId nodeId, bool isHidden) => this.hidden[nodeId] = isHidden;
}
