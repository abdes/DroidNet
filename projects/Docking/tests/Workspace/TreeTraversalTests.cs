// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using static TreeTraversal;

/// <summary>
/// Unit test cases for the traversal algorithm of binary trees.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TreeTraversal))]
public partial class TreeTraversalTests : IDisposable
{
    private bool disposed;

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void InOrderTraversal_EmptyTree()
    {
        // Setup
        var root = new SimpleBinaryTreeNode(0);

        // Act
        var flattened = Flatten(root);

        // Assert
        _ = flattened.Should().HaveCount(1).And.ContainInConsecutiveOrder([0]);
    }

    [TestMethod]
    public void InOrderTraversal_OneChildLeft()
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
    public void InOrderTraversal_OneChildRight()
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
    public void InOrderTraversal_TwoChildren()
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
    public void InOrderTraversal_LeftSubTree()
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
    public void InOrderTraversal_RightSubTree()
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
    public void InOrderTraversal_FullTree()
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
