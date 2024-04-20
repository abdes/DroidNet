// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using static TreeTraversal;

/// <summary>Unit test cases for the <see cref="BinaryTreeNode{T}" /> class.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(DockingTreeNode))]
public class DockingTreeNodeTests : IDisposable
{
    private readonly DummyDocker docker = new();

    private bool disposed;

    public void Dispose()
    {
        if (!this.disposed)
        {
            return;
        }

        this.docker.Dispose();

        GC.SuppressFinalize(this);
        this.disposed = true;
    }

    [TestMethod]
    public void AddChildLeft_BothSlotsEmpty()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        root.AddChildLeft(newChild, DockGroupOrientation.Vertical);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().HaveCount(2).And.ContainInConsecutiveOrder(newChild.Value, root.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(
                DockGroupOrientation.Undetermined,
                "orientation is applied only if the parent segment has more than one child");
    }

    [TestMethod]
    public void AddChildLeft_EmptyLeftSlot()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Right = rightChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(newChild.Value, root.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildLeft_OccupiedLeftSlotEmptyRightSlot()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Left = leftChild };

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(newChild.Value, root.Value, leftChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildLeft_BothSlotsOccupiedLeftIsLayoutGroup()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(4)
            .And.ContainInConsecutiveOrder(newChild.Value, leftChild.Value, root.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(
                DockGroupOrientation.Undetermined,
                "orientation is applied only if the parent segment has more than one child");
    }

    [TestMethod]
    public void AddChildLeft_BothSlotsOccupiedLeftIsDockGroup()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };
        var originalFlattenedNodes = Flatten(root);
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().HaveCount(5);
        _ = flattenedNodes[0].Should().Be(newChild.Value);
        _ = flattenedNodes.Should().ContainInConsecutiveOrder(originalFlattenedNodes);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildRight_BothSlotsEmpty()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        root.AddChildRight(newChild, DockGroupOrientation.Vertical);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().HaveCount(2).And.ContainInConsecutiveOrder(root.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(
                DockGroupOrientation.Undetermined,
                "orientation is applied only if the parent segment has more than one child");
    }

    [TestMethod]
    public void AddChildRight_EmptyRightSlot()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Left = leftChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildRight(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(leftChild.Value, root.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildRight_OccupiedRightSlotEmptyLeftSlot()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Right = rightChild };

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildRight(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(rightChild.Value, root.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildRight_BothSlotsOccupiedRightIsLayoutGroup()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildRight(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(4)
            .And.ContainInConsecutiveOrder(leftChild.Value, root.Value, rightChild.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(
                DockGroupOrientation.Undetermined,
                "orientation is applied only if the parent segment has more than one child");
    }

    [TestMethod]
    public void AddChildRight_BothSlotsOccupiedRightIsDockGroup()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };
        var originalFlattenedNodes = Flatten(root);
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildRight(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().HaveCount(5);
        _ = flattenedNodes[^1].Should().Be(newChild.Value);
        _ = flattenedNodes.Should().ContainInConsecutiveOrder(leftChild.Value, root.Value);
        _ = flattenedNodes.Should().ContainInConsecutiveOrder(originalFlattenedNodes);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }
}
