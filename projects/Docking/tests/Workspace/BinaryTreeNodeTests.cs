// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;

#pragma warning disable CA2000 // Dispose objects before losing scope

/// <summary>
/// Unit test cases for the <see cref="BinaryTreeNode{T}" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("BinaryTreeNode")]
public sealed partial class BinaryTreeNodeTests : IDisposable
{
    private readonly DummyDocker docker = new();

    private bool disposed;

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
    public void SetLeft_Should_UpdateParentRelationship_When_NewLeftNode()
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
    public void SetLeft_Should_HaveNoEffect_When_SameLeftNode()
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
    public void SetLeft_Should_UpdateParentRelationship_When_DifferentLeftNode()
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
    public void SetLeft_Should_AcceptNullValue()
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
    public void SetRight_Should_UpdateParentRelationship_When_NewRightNode()
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
    public void SetRight_Should_HaveNoEffect_When_SameRightNode()
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
    public void SetRight_Should_UpdateParentRelationship_When_DifferentRightNode()
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
    public void SetRight_Should_AcceptNullValue()
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
    public void Sibling_Should_ReturnNull_When_NoParent()
    {
        // Arrange
        var node = MakeSimpleNode();

        // Act
        var sibling = node.Sibling;

        // Assert
        _ = sibling.Should().BeNull();
    }

    [TestMethod]
    public void Sibling_Should_ReturnRightChild_When_ThisIsLeftChild()
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
    public void Sibling_Should_ReturnLeftChild_When_ThisIsRightChild()
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
