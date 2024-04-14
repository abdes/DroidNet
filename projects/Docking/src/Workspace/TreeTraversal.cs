// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics;

/// <summary>Docking tree traversal algorithms.</summary>
internal static class TreeTraversal
{
    /// <summary>The visitor that will be invoked during a tree traversal.</summary>
    /// <typeparam name="T">The type of values stored in the tree nodes.</typeparam>
    /// <param name="node">The node being visited.</param>
    /// <returns><see langword="true" /> if the traversal should continue; <see langword="false" /> if not.</returns>
    public delegate bool Visitor<T>(BinaryTreeNode<T>? node);

    /// <summary>
    /// Traverses the sub-tree rooted at the given <paramref name="node" /> using a depth-first algorithm with in-order traversal.
    /// </summary>
    /// <typeparam name="T">The type of values stored in the tree nodes.</typeparam>
    /// <param name="node">The root of the (sub-)tree to be traversed.</param>
    /// <param name="visit">The node visitor that will be invoked when a node is visted.</param>
    /// <remarks>
    /// In this traversal, the traversal goes as deep as possible and then visits in order, the left child, the parent node and
    /// then the right child. The steps can be summarized as following:
    /// <list type="number">
    /// <item>Recursively traverse the current node's left subtree.</item>
    /// <item>Visit the current node.</item>
    /// <item>Recursively traverse the current node's right subtree.</item>
    /// </list>
    /// </remarks>
    public static void DepthFirstInOrder<T>(BinaryTreeNode<T> node, Visitor<T> visit)
    {
        /*
         * We're using an in-order traversal of the tree without recursion by tracking the recently traversed nodes to
         * identify the next one.
         */
        var currentNode = node;
        BinaryTreeNode<T>? previousNode = null;
        while (currentNode != null)
        {
            BinaryTreeNode<T>? nextNode;
            if (currentNode.Right != null && previousNode == currentNode.Right)
            {
                nextNode = currentNode.Parent;
            }
            else if (currentNode.Left == null || previousNode == currentNode.Left)
            {
                // Visit the current node
                Debug.WriteLine($"Visiting {currentNode}");
                var continueTraversal = visit(currentNode);
                if (!continueTraversal)
                {
                    return;
                }

                nextNode = currentNode.Right ?? currentNode.Parent;
            }
            else
            {
                nextNode = currentNode.Left;
            }

            previousNode = currentNode;
            currentNode = nextNode;
        }
    }

    /// <summary>
    /// Get a flattened version of the (sub-)tree rooted at the given <paramref name="node" /> as a list resulting from a
    /// depth-first, in-order, traversal of the (sub-)tree.
    /// </summary>
    /// <typeparam name="T">The type of values stored in the tree nodes.</typeparam>
    /// <param name="node">The root of the (sub-)tree to be flattened.</param>
    /// <returns>A list containing the (sub-)tree nodes in order after a depth-first traversal.</returns>
    public static List<T> Flatten<T>(BinaryTreeNode<T> node)
    {
        var flattened = new List<T>();

        bool Visitor(BinaryTreeNode<T>? node)
        {
            if (node is not null)
            {
                flattened.Add(node.Value);
            }

            return true;
        }

        DepthFirstInOrder(node, Visitor);
        return flattened;
    }
}
