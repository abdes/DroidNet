// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Utils;

/// <summary>
///     Provides efficient scene hierarchy traversal utilities.
/// </summary>
public static class SceneTraversal
{
    /// <summary>
    ///     Performs a depth-first traversal of the scene hierarchy starting from the given node.
    /// </summary>
    /// <param name="root">The root node to start traversal from.</param>
    /// <param name="visitor">Action invoked for each node, receiving the node and its parent GUID.</param>
    /// <param name="parentGuid">Optional parent GUID for the root node.</param>
    public static void TraverseDepthFirst(SceneNode root, Action<SceneNode, Guid?> visitor, Guid? parentGuid = null)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentNullException.ThrowIfNull(visitor);

        visitor(root, parentGuid);

        foreach (var child in root.Children)
        {
            TraverseDepthFirst(child, visitor, root.Id);
        }
    }

    /// <summary>
    ///     Performs a depth-first traversal with async visitor support.
    /// </summary>
    /// <param name="root">The root node to start traversal from.</param>
    /// <param name="visitor">Async function invoked for each node, receiving the node and its parent GUID.</param>
    /// <param name="parentGuid">Optional parent GUID for the root node.</param>
    /// <returns>A task that completes when traversal is finished.</returns>
    public static async Task TraverseDepthFirstAsync(
        SceneNode root,
        Func<SceneNode, Guid?, Task> visitor,
        Guid? parentGuid = null)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentNullException.ThrowIfNull(visitor);

        await visitor(root, parentGuid).ConfigureAwait(false);

        foreach (var child in root.Children)
        {
            await TraverseDepthFirstAsync(child, visitor, root.Id).ConfigureAwait(false);
        }
    }

    /// <summary>
    ///     Collects all node GUIDs in depth-first order.
    /// </summary>
    /// <param name="root">The root node to start collection from.</param>
    /// <returns>List of all node GUIDs in the hierarchy.</returns>
    public static IReadOnlyCollection<Guid> CollectNodeIds(SceneNode root)
    {
        ArgumentNullException.ThrowIfNull(root);

        var ids = new List<Guid>();
        TraverseDepthFirst(root, (node, _) => ids.Add(node.Id));
        return ids;
    }

    /// <summary>
    ///     Collects all nodes in depth-first order.
    /// </summary>
    /// <param name="root">The root node to start collection from.</param>
    /// <returns>List of all nodes in the hierarchy.</returns>
    public static IReadOnlyCollection<SceneNode> CollectNodes(SceneNode root)
    {
        ArgumentNullException.ThrowIfNull(root);

        var nodes = new List<SceneNode>();
        TraverseDepthFirst(root, (node, _) => nodes.Add(node));
        return nodes;
    }

    /// <summary>
    ///     Finds a node by its GUID within a hierarchy.
    /// </summary>
    /// <param name="root">The root node to search from.</param>
    /// <param name="targetId">The GUID to search for.</param>
    /// <returns>The found node, or null if not found.</returns>
    public static SceneNode? FindNodeById(SceneNode root, Guid targetId)
    {
        ArgumentNullException.ThrowIfNull(root);

        if (root.Id == targetId)
        {
            return root;
        }

        foreach (var child in root.Children)
        {
            var found = FindNodeById(child, targetId);
            if (found is not null)
            {
                return found;
            }
        }

        return null;
    }
}
