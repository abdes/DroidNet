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
public sealed partial class DockingTreeNodeTests : IDisposable
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

        GC.SuppressFinalize(this);
        this.disposed = true;
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.AddChildLeft" /> method.</summary>
public partial class DockingTreeNodeTests
{
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
    public void AddChildLeft_ToEdgeGroup()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new EdgeGroup(this.docker, DockGroupOrientation.Horizontal))
        {
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildLeft(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(newChild.Value, root.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(DockGroupOrientation.Horizontal, "edge group orientation is never changed");
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.AddChildRight" /> method.</summary>
public partial class DockingTreeNodeTests
{
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

    [TestMethod]
    public void AddChildRight_ToEdgeGroup()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new EdgeGroup(this.docker, DockGroupOrientation.Horizontal))
        {
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildRight(newChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(rightChild.Value, root.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should()
            .Be(DockGroupOrientation.Horizontal, "edge group orientation is never changed");
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.AddChildBefore" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void AddChildBefore_BothSlotsEmpty()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var sibling = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.AddChildBefore(newChild, sibling, DockGroupOrientation.Vertical);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AddChildBefore_SiblingIsNotChild()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        var sibling = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.AddChildBefore(newChild, sibling, DockGroupOrientation.Vertical);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AddChildBefore_SiblingIsLeft_RightIsFree()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Left = leftChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildBefore(newChild, leftChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(newChild.Value, root.Value, leftChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildBefore_SiblingIsLeft_RightIsFull()
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
        root.AddChildBefore(newChild, leftChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(newChild.Value, leftChild.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildBefore_SiblingIsRight_LeftIsFree()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Right = rightChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildBefore(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(newChild.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildBefore_SiblingIsRight_LeftIsFull()
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
        root.AddChildBefore(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(leftChild.Value, newChild.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildBefore_ToEdgeGroup()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new EdgeGroup(this.docker, DockGroupOrientation.Horizontal))
        {
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildBefore(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(newChild.Value, rightChild.Value);
        _ = root.Value.Orientation.Should().Be(DockGroupOrientation.Horizontal);
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.AddChildAfter" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void AddChildAfter_BothSlotsEmpty()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var sibling = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.AddChildAfter(newChild, sibling, DockGroupOrientation.Vertical);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AddChildAfter_SiblingIsNotChild()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        var sibling = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.AddChildAfter(newChild, sibling, DockGroupOrientation.Vertical);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AddChildAfter_SiblingIsLeft_RightIsFree()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Left = leftChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildAfter(newChild, leftChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should()
            .HaveCount(3)
            .And.ContainInConsecutiveOrder(leftChild.Value, root.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildAfter_SiblingIsLeft_RightIsFull()
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
        root.AddChildAfter(newChild, leftChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(leftChild.Value, newChild.Value, rightChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildAfter_SiblingIsRight_LeftIsFree()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Right = rightChild };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Horizontal;
        root.AddChildAfter(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(rightChild.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildAfter_SiblingIsRight_LeftIsFull()
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
        root.AddChildAfter(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(leftChild.Value, rightChild.Value, newChild.Value);
        _ = newChild.Parent!.Value.Orientation.Should().Be(desiredOrientation);
    }

    [TestMethod]
    public void AddChildAfter_ToEdgeGroup()
    {
        // Arrange
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new EdgeGroup(this.docker, DockGroupOrientation.Horizontal))
        {
            Right = rightChild,
        };
        var newChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        root.AddChildAfter(newChild, rightChild, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Should().ContainInOrder(rightChild.Value, newChild.Value);
        _ = root.Value.Orientation.Should().Be(DockGroupOrientation.Horizontal);
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.RemoveChild" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void RemoveChild_NotAChild()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var child = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.RemoveChild(child);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void RemoveChild_CenterGroup()
    {
        // Arrange
        var child = new DockingTreeNode(this.docker, new CenterGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker)) { Left = child };

        // Act
        var act = () => root.RemoveChild(child);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void RemoveChild_Left()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        root.RemoveChild(leftChild);

        // Assert
        _ = root.Left.Should().BeNull();
        _ = root.Right.Should().Be(rightChild);
    }

    [TestMethod]
    public void RemoveChild_Right()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        root.RemoveChild(rightChild);

        // Assert
        _ = root.Right.Should().BeNull();
        _ = root.Left.Should().Be(leftChild);
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.MergeLeafParts" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void MergeLeafParts_NullPart_Throws()
    {
        // Arrange
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker));

        // Act
        var act = () => root.MergeLeafParts();

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void MergeLeafParts_NonLeafPart_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // No leaf right child
        var rightChild = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker)),
        };
        _ = rightChild.IsLeaf.Should().BeFalse();

        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        var act = () => root.MergeLeafParts();

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void MergeLeafParts_LeftIsCenterGroup_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new CenterGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        var act = () => root.MergeLeafParts();

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void MergeLeafParts_RightIsCenterGroup_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new CenterGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        var act = () => root.MergeLeafParts();

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void MergeLeafParts_MergesDocks()
    {
        // Arrange
        var leftSegment = new LayoutDockGroup(this.docker);
        leftSegment.AddDock(new SimpleDock());
        var leftChild = new DockingTreeNode(this.docker, leftSegment);

        var rightSegment = new LayoutDockGroup(this.docker);
        rightSegment.AddDock(new SimpleDock());
        rightSegment.AddDock(new SimpleDock());
        var rightChild = new DockingTreeNode(this.docker, rightSegment);

        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        root.MergeLeafParts();

        // Assert
        var oneChild = root.Left ?? root.Right;
        _ = oneChild.Sibling.Should().BeNull();
        _ = oneChild.Value.Should().BeOfType<LayoutDockGroup>();
        var docksCount = ((LayoutDockGroup)oneChild.Value).Docks.Count;
        _ = docksCount.Should().Be(leftSegment.Docks.Count + rightSegment.Docks.Count);
        if (docksCount > 1)
        {
            _ = oneChild.Value.Orientation.Should()
                .Be(oneChild.Parent!.Value.Orientation, "the merged child takes the orientation of the parent");
        }
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.AssimilateChild" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void AssimilateChild_NotLoneChild_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
            Right = rightChild,
        };

        // Act
        var act = () => root.AssimilateChild(leftChild);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AssimilateChild_NotChild_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
        };
        var child = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));

        // Act
        var act = () => root.AssimilateChild(child);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AssimilateChild_ChildIsCenterGroup_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new CenterGroup(this.docker));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
        };

        // Act
        var act = () => root.AssimilateChild(leftChild);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AssimilateChild_ChildIsEdgeGroup_Throws()
    {
        // Arrange
        var leftChild = new DockingTreeNode(this.docker, new EdgeGroup(this.docker, DockGroupOrientation.Horizontal));
        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
        };

        // Act
        var act = () => root.AssimilateChild(leftChild);

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AssimilateChild_ChildWithChildren()
    {
        // Arrange
        var leftSubChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        var rightSubChild = new DockingTreeNode(this.docker, new LayoutDockGroup(this.docker));
        const DockGroupOrientation childOrientation = DockGroupOrientation.Horizontal;
        var leftChild
            = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker, childOrientation))
            {
                Left = leftSubChild,
                Right = rightSubChild,
            };

        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
        };

        // Act
        root.AssimilateChild(leftChild);

        // Assert
        _ = root.Left.Should().Be(leftSubChild);
        _ = root.Right.Should().Be(rightSubChild);
        _ = root.Value.Orientation.Should().Be(childOrientation);
    }

    [TestMethod]
    public void AssimilateChild_ChildWithDocks()
    {
        // Arrange
        const DockGroupOrientation childOrientation = DockGroupOrientation.Horizontal;
        var childSegment = new LayoutDockGroup(this.docker) { Orientation = childOrientation };
        childSegment.AddDock(new SimpleDock());
        childSegment.AddDock(new SimpleDock());
        var leftChild = new DockingTreeNode(this.docker, childSegment);

        var root = new MockDockingTreeNode(this.docker, new LayoutGroup(this.docker))
        {
            Left = leftChild,
        };

        // Act
        root.AssimilateChild(leftChild);

        // Assert
        _ = root.Left.Should().BeNull();
        _ = root.Right.Should().BeNull();
        _ = root.Value.Should().BeOfType<LayoutDockGroup>();
        _ = ((LayoutDockGroup)root.Value).Docks.Count.Should().Be(2);
    }
}

/// <summary>Unit test cases for the <see cref="DockingTreeNode.Repartition" /> method.</summary>
public partial class DockingTreeNodeTests
{
    [TestMethod]
    public void Repartition_NotLayoutDockGroup_Throws()
    {
        // Arrange
        var root = new DockingTreeNode(this.docker, new LayoutGroup(this.docker));

        // Act
        var act = () =>
        {
            using var dock = new SimpleDock();
            return root.Repartition(dock, DockGroupOrientation.Horizontal);
        };

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void Repartition_RelativeDockDoesNotBelongToThisNode_Throws()
    {
        // Arrange
        const DockGroupOrientation existingOrientation = DockGroupOrientation.Horizontal;
        var segment = new LayoutDockGroup(this.docker, existingOrientation);
        var root = new DockingTreeNode(this.docker, segment);

        // Act
        var act = () =>
        {
            using var dock = new SimpleDock();
            return root.Repartition(dock, DockGroupOrientation.Horizontal);
        };

        // Assert
        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Repartition_NothingBeforeOrAfter()
    {
        // Arrange
        const DockGroupOrientation existingOrientation = DockGroupOrientation.Horizontal;
        var segment = new LayoutDockGroup(this.docker, existingOrientation);
        using var relativeDock = new SimpleDock();
        segment.AddDock(relativeDock);
        var root = new DockingTreeNode(this.docker, segment);

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        var anchorSegment = root.Repartition(relativeDock, desiredOrientation);

        // Assert
        _ = root.Left.Should().NotBeNull();
        var leftSegment = root.Left!.Value as LayoutDockGroup;
        _ = leftSegment.Should().NotBeNull();
        _ = leftSegment!.Docks.Should().BeEquivalentTo([relativeDock]);
        _ = leftSegment.Orientation.Should().Be(desiredOrientation);

        _ = root.Right.Should().BeNull();

        _ = root.Value.Orientation.Should().Be(existingOrientation);
    }

    [TestMethod]
    public void Repartition_NothingBefore()
    {
        // Arrange
        const DockGroupOrientation existingOrientation = DockGroupOrientation.Horizontal;
        var segment = new LayoutDockGroup(this.docker, existingOrientation);
        using var relativeDock = new SimpleDock();
        segment.AddDock(relativeDock);
        using var afterDock = new SimpleDock();
        segment.AddDock(afterDock);
        var root = new DockingTreeNode(this.docker, segment);

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        var anchorSegment = root.Repartition(relativeDock, desiredOrientation);

        // Assert
        _ = root.Left.Should().NotBeNull();
        var leftSegment = root.Left!.Value as LayoutDockGroup;
        _ = leftSegment.Should().NotBeNull();
        _ = leftSegment!.Docks.Should().BeEquivalentTo([relativeDock]);
        _ = leftSegment.Orientation.Should().Be(desiredOrientation);

        _ = root.Right.Should().NotBeNull();
        var rightSegment = root.Right!.Value as LayoutDockGroup;
        _ = rightSegment.Should().NotBeNull();
        _ = rightSegment!.Docks.Should().BeEquivalentTo([afterDock]);
        _ = rightSegment.Orientation.Should()
            .Be(rightSegment!.Docks.Count > 1 ? existingOrientation : DockGroupOrientation.Undetermined);

        _ = root.Value.Orientation.Should().Be(existingOrientation);
    }

    [TestMethod]
    public void Repartition_NothingAfter()
    {
        // Arrange
        const DockGroupOrientation existingOrientation = DockGroupOrientation.Horizontal;
        var segment = new LayoutDockGroup(this.docker, existingOrientation);
        using var beforeDock = new SimpleDock();
        segment.AddDock(beforeDock);
        using var relativeDock = new SimpleDock();
        segment.AddDock(relativeDock);
        var root = new DockingTreeNode(this.docker, segment);

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        var anchorSegment = root.Repartition(relativeDock, desiredOrientation);

        // Assert
        _ = root.Left.Should().NotBeNull();
        var leftSegment = root.Left!.Value as LayoutDockGroup;
        _ = leftSegment.Should().NotBeNull();
        _ = leftSegment!.Docks.Should().BeEquivalentTo([beforeDock]);
        _ = leftSegment.Orientation.Should()
            .Be(leftSegment!.Docks.Count > 1 ? existingOrientation : DockGroupOrientation.Undetermined);

        _ = root.Right.Should().NotBeNull();
        var rightSegment = root.Right!.Value as LayoutDockGroup;
        _ = rightSegment.Should().NotBeNull();
        _ = rightSegment!.Docks.Should().BeEquivalentTo([relativeDock]);
        _ = rightSegment.Orientation.Should().Be(desiredOrientation);

        _ = root.Value.Orientation.Should().Be(existingOrientation);
    }

    [TestMethod]
    public void Repartition_BeforeAndAfter()
    {
        // Arrange
        const DockGroupOrientation existingOrientation = DockGroupOrientation.Horizontal;
        var segment = new LayoutDockGroup(this.docker, existingOrientation);
        using var beforeDock = new SimpleDock();
        segment.AddDock(beforeDock);
        using var relativeDock = new SimpleDock();
        segment.AddDock(relativeDock);
        using var afterDock = new SimpleDock();
        segment.AddDock(afterDock);
        var root = new DockingTreeNode(this.docker, segment);

        // Act
        const DockGroupOrientation desiredOrientation = DockGroupOrientation.Vertical;
        var anchorSegment = root.Repartition(relativeDock, desiredOrientation);

        // Assert
        var flattenedNodes = Flatten(root);
        _ = flattenedNodes.Count.Should().Be(5);
        var docks = new List<IDock>();
        foreach (var node in flattenedNodes)
        {
            if (node is DockGroup withDocks)
            {
                if (withDocks.Docks.Contains(relativeDock))
                {
                    _ = withDocks.Orientation.Should().Be(desiredOrientation);
                }

                docks.AddRange(withDocks.Docks);
            }
            else
            {
                _ = node.Orientation.Should().Be(existingOrientation);
            }
        }

        _ = docks.Count.Should().Be(3);
        _ = docks.Should().ContainInConsecutiveOrder(beforeDock, relativeDock, afterDock);

        _ = root.Value.Orientation.Should().Be(existingOrientation);
    }
}
