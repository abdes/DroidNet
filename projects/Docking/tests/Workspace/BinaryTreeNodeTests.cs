// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Tests.Mocks;
using FluentAssertions;

namespace DroidNet.Docking.Tests.Workspace;

#pragma warning disable CA2000 // Dispose objects before losing scope

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("BinaryTreeNode")]
public sealed partial class BinaryTreeNodeTests : IDisposable
{
    private readonly DummyDocker docker = new();

    private bool disposed;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.docker.Dispose();

        this.disposed = true;
    }

    [TestMethod]
    public void SetLeftShouldUpdateParentRelationshipWhenNewLeftNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var newNode = MakeSimpleNode();

        // Act
        parent.Left = newNode;

        // Assert
        _ = parent.Left.Should().Be(newNode);
        _ = newNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetLeftShouldHaveNoEffectWhenSameLeftNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var existingNode = MakeSimpleNode();
        parent.Left = existingNode;

        // Act
        parent.Left = existingNode;

        // Assert
        _ = parent.Left.Should().Be(existingNode);
        _ = existingNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetLeftShouldUpdateParentRelationshipWhenDifferentLeftNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var oldNode = MakeSimpleNode();
        var newNode = MakeSimpleNode();
        parent.Left = oldNode;

        // Act
        parent.Left = newNode;

        // Assert
        _ = parent.Left.Should().Be(newNode);
        _ = oldNode.Parent.Should().BeNull();
        _ = newNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetLeftShouldAcceptNullValue()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var oldNode = MakeSimpleNode();
        parent.Left = oldNode;

        // Act
        parent.Left = null;

        // Assert
        _ = parent.Left.Should().BeNull();
        _ = oldNode.Parent.Should().BeNull();
    }

    [TestMethod]
    public void SetRightShouldUpdateParentRelationshipWhenNewRightNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var newNode = MakeSimpleNode();

        // Act
        parent.Right = newNode;

        // Assert
        _ = parent.Right.Should().Be(newNode);
        _ = newNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetRightShouldHaveNoEffectWhenSameRightNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var existingNode = MakeSimpleNode();
        parent.Right = existingNode;

        // Act
        parent.Right = existingNode;

        // Assert
        _ = parent.Right.Should().Be(existingNode);
        _ = existingNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetRightShouldUpdateParentRelationshipWhenDifferentRightNode()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var oldNode = MakeSimpleNode();
        var newNode = MakeSimpleNode();
        parent.Right = oldNode;

        // Act
        parent.Right = newNode;

        // Assert
        _ = parent.Right.Should().Be(newNode);
        _ = oldNode.Parent.Should().BeNull();
        _ = newNode.Parent.Should().Be(parent);
    }

    [TestMethod]
    public void SetRightShouldAcceptNullValue()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var oldNode = MakeSimpleNode();
        parent.Right = oldNode;

        // Act
        parent.Right = null;

        // Assert
        _ = parent.Right.Should().BeNull();
        _ = oldNode.Parent.Should().BeNull();
    }

    [TestMethod]
    public void SiblingShouldReturnNullWhenNoParent()
    {
        // Arrange
        var node = MakeSimpleNode();

        // Act
        var sibling = node.Sibling;

        // Assert
        _ = sibling.Should().BeNull();
    }

    [TestMethod]
    public void SiblingShouldReturnRightChildWhenThisIsLeftChild()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var leftChild = MakeSimpleNode();
        var rightChild = MakeSimpleNode();
        parent.Left = leftChild;
        parent.Right = rightChild;

        // Act
        var sibling = leftChild.Sibling;

        // Assert
        _ = sibling.Should().Be(rightChild);
    }

    [TestMethod]
    public void SiblingShouldReturnLeftChildWhenThisIsRightChild()
    {
        // Arrange
        using var parent = MakeSimpleNode();
        var leftChild = MakeSimpleNode();
        var rightChild = MakeSimpleNode();
        parent.Left = leftChild;
        parent.Right = rightChild;

        // Act
        var sibling = rightChild.Sibling;

        // Assert
        _ = sibling.Should().Be(leftChild);
    }

    private static SimpleBinaryTreeNode MakeSimpleNode() => new(0);
}
#pragma warning restore CA2000 // Dispose objects before losing scope
