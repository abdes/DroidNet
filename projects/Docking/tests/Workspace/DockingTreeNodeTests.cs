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
[TestCategory("BinaryTreeNode")]
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
    public void AddChildLeft_EmptyLeftSlot()
    {
        // Arrange
        var segment = new LayoutGroup(this.docker);
        var node = new DockingTreeNode(this.docker, segment);
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        node.AddChildLeft(newChild, DockGroupOrientation.Vertical);

        // Assert
        var flattenedNodes = Flatten(node);
        _ = flattenedNodes.Should().HaveCount(2).And.ContainInConsecutiveOrder(newChild.Value, segment);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(
                DockGroupOrientation.Undetermined,
                "orientation is applied only if the parent segment has more than one child");
    }

    [TestMethod]
    public void AddChildLeft_OccupiedLeftSlotEmptyRightSlot()
    {
        // Arrange
        var segment = new LayoutGroup(this.docker);
        var node = new MockDockingTreeNode(this.docker, segment);
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        node.Left = leftChild;

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        node.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(node);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(newChild.Value, segment, leftChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildLeft_BothSlotsOccupiedLeftIsLayoutGroup()
    {
        // Arrange
        var segment = new LayoutGroup(this.docker);
        var leftChild = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var node = new MockDockingTreeNode(this.docker, segment)
        {
            Left = leftChild,
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        node.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(node);
        _ = flattenedNodes.Should()
            .HaveCount(4)
            .And.ContainInConsecutiveOrder(newChild.Value, leftChild.Value, segment, rightChild.Value);
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
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().HaveCount(5);
        _ = flattenedNodes[0].Should().Be(newChild.Value);
        _ = flattenedNodes[1].Should().Be(leftChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }
}
