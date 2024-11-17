// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Tests.Mocks;
using DroidNet.Docking.Workspace;
using FluentAssertions;

using static DroidNet.Docking.Workspace.TreeTraversal;

namespace DroidNet.Docking.Tests.Workspace;

#pragma warning disable CA2000 // Dispose objects before losing scope

/// <summary>
/// Unit test cases for the traversal algorithm of binary trees.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TreeTraversal))]
public sealed partial class TreeTraversalTests : IDisposable
{
    private bool disposed;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
    }

    [TestMethod]
    public void InOrderTraversalEmptyTree()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(0);

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(1).And.ContainInConsecutiveOrder([0]);
    }

    [TestMethod]
    public void InOrderTraversalOneChildLeft()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(2)
        {
            Left = new SimpleBinaryTreeNode(1),
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(2).And.ContainInConsecutiveOrder([1, 2]);
    }

    [TestMethod]
    public void InOrderTraversalOneChildRight()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(1)
        {
            Right = new SimpleBinaryTreeNode(2),
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(2).And.ContainInConsecutiveOrder([1, 2]);
    }

    [TestMethod]
    public void InOrderTraversalTwoChildren()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(2)
        {
            Left = new SimpleBinaryTreeNode(1),
            Right = new SimpleBinaryTreeNode(3),
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(3).And.ContainInConsecutiveOrder([1, 2, 3]);
    }

    [TestMethod]
    public void InOrderTraversalLeftSubTree()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(4)
        {
            Left = new SimpleBinaryTreeNode(3)
            {
                Left = new SimpleBinaryTreeNode(1)
                {
                    Right = new SimpleBinaryTreeNode(2),
                },
            },
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(4).And.ContainInConsecutiveOrder([1, 2, 3, 4]);
    }

    [TestMethod]
    public void InOrderTraversalRightSubTree()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(1)
        {
            Right = new SimpleBinaryTreeNode(4)
            {
                Left = new SimpleBinaryTreeNode(2)
                {
                    Right = new SimpleBinaryTreeNode(3),
                },
                Right = new SimpleBinaryTreeNode(5),
            },
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(5).And.ContainInConsecutiveOrder([1, 2, 3, 4, 5]);
    }

    [TestMethod]
    public void InOrderTraversalFullTree()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(4)
        {
            Left = new SimpleBinaryTreeNode(3)
            {
                Left = new SimpleBinaryTreeNode(1)
                {
                    Right = new SimpleBinaryTreeNode(2),
                },
            },
            Right = new SimpleBinaryTreeNode(8)
            {
                Left = new SimpleBinaryTreeNode(6)
                {
                    Left = new SimpleBinaryTreeNode(5),
                    Right = new SimpleBinaryTreeNode(7),
                },
                Right = new SimpleBinaryTreeNode(9),
            },
        };

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(9).And.ContainInConsecutiveOrder([1, 2, 3, 4, 5]);
    }
}
#pragma warning restore CA2000 // Dispose objects before losing scope
