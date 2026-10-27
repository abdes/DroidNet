// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>
/// The editor's hidden and locked nodes as a scratch validation sees them: hiding and retargeting change only the scratch
/// copy.
/// </summary>
/// <param name="live">The workspace's interaction state, which the scratch reads and never writes.</param>
internal sealed class ScratchNodeInteraction(ISceneNodeInteraction live) : ISceneNodeInteraction
{
    private readonly Dictionary<NodeReference, bool> hidden = [];
    private readonly Dictionary<NodeReference, bool> locked = [];

    /// <inheritdoc/>
    public bool IsHidden(NodeReference node) => this.hidden.TryGetValue(node, out var isHidden) ? isHidden : live.IsHidden(node);

    /// <inheritdoc/>
    public bool IsLocked(NodeReference node) => this.locked.TryGetValue(node, out var isLocked) ? isLocked : live.IsLocked(node);

    /// <inheritdoc/>
    public SceneNode? GetLockOwner(SceneNode node) => live.GetLockOwner(node);

    /// <inheritdoc/>
    public IReadOnlyList<NodeReference>? GetAncestors(Scene scene, NodeReference node) => live.GetAncestors(scene, node);

    /// <inheritdoc/>
    public NodeLock GetLock(Scene scene, NodeReference node) => live.GetLock(scene, node);

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> LockedNodes()
        => [.. live.LockedNodes().Where(node => !this.locked.ContainsKey(node)).Concat(this.locked.Where(static entry => entry.Value).Select(static entry => entry.Key)).Distinct()];

    /// <inheritdoc/>
    public void SetHidden(NodeReference node, bool isHidden) => this.hidden[node] = isHidden;

    /// <inheritdoc/>
    public IReadOnlyList<NodeReference> Retarget(Func<NodeReference, NodeReference?> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var moved = new List<NodeReference>();
        foreach (var node in this.LockedNodes())
        {
            if (map(node) is { } target && target != node)
            {
                this.locked[node] = false;
                this.locked[target] = true;
            }
        }

        foreach (var node in this.hidden.Where(static entry => entry.Value).Select(static entry => entry.Key).ToList())
        {
            if (map(node) is { } target && target != node)
            {
                this.hidden[node] = false;
                this.hidden[target] = true;
                moved.Add(target);
            }
        }

        return moved;
    }
}
