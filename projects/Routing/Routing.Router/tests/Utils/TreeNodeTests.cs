// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Utils;
using AwesomeAssertions;

namespace DroidNet.Routing.Tests.Utils;

/// <summary>
/// Contains unit test cases for the <see cref="TreeNode" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("URL Tree")]
public class TreeNodeTests
{
    private readonly TestableTreeNode sut;

    /// <summary>
    /// Initializes a new instance of the <see cref="TreeNodeTests" /> class.
    /// </summary>
    /// <remarks>
    /// A new instance of the test class is created for each test case.
    /// </remarks>
    public TreeNodeTests()
    {
        this.sut = new TestableTreeNode();
    }

    /// <summary>
    /// Tests the <see cref="TreeNode.Root" /> property when the node is the
    /// root.
    /// </summary>
    [TestMethod]
    public void Root_WhenNodeIsRoot_ReturnsSelf()
    {
        var root = this.sut.Root;

        _ = root.Should().Be(this.sut);
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.Root" /> property, when called on
    /// a node that has a parent, will return the parent's root.
    /// </summary>
    [TestMethod]
    public void Root_WhenNodeHasParent_ReturnsRootOfParent()
    {
        var parent = new TestableTreeNode();
        parent.AddChild(this.sut);

        var root = this.sut.Root;

        _ = root.Should().Be(parent.Root);
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.ClearChildren" /> method removes
    /// all children from the node.
    /// </summary>
    [TestMethod]
    public void ClearChildren_RemovesAllChildren()
    {
        const int numChildren = 3;
        for (var i = 0; i < numChildren; i++)
        {
            this.sut.AddChild(new TestableTreeNode());
        }

        _ = this.sut.Children.Should().HaveCount(numChildren);

        this.sut.ClearChildren();

        var children = this.sut.Children;
        _ = children.Should().BeEmpty();
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.ClearChildren" /> still works
    /// when the node has no children.
    /// </summary>
    [TestMethod]
    public void ClearChildren_WhenNoChildren_StillWorks()
    {
        _ = this.sut.Children.Should().BeEmpty();

        var action = this.sut.ClearChildren;

        _ = action.Should().NotThrow();
        var children = this.sut.Children;
        _ = children.Should().BeEmpty();
    }

    /// <summary>
    /// Verifies that <see cref="TreeNode.AddChild" /> properly adds the child
    /// to the node and updates the child's parent.
    /// </summary>
    [TestMethod]
    public void AddChild_AddsChildCorrectly()
    {
        var child = new TestableTreeNode();
        this.sut.AddChild(child);

        _ = this.sut.Children.Should().ContainSingle().Which.Should().Be(child);
        _ = child.Parent.Should().Be(this.sut);
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.RemoveChild" /> properly removes
    /// an existing child and clears its parent property.
    /// </summary>
    [TestMethod]
    public void RemoveChild_RemovesChildCorrectly()
    {
        var child = new TestableTreeNode();
        this.sut.AddChild(child);
        _ = this.sut.Children.Should().ContainSingle().Which.Should().Be(child);

        var removed = this.sut.RemoveChild(child);

        var children = this.sut.Children;
        _ = removed.Should().BeTrue();
        _ = children.Should().BeEmpty();
        _ = child.Parent.Should().BeNull();
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.RemoveChild" /> returns
    /// <see langword="false" /> when the child does not exist.
    /// </summary>
    [TestMethod]
    public void RemoveChild_WhenChildDoesNotExist_ReturnsFalse()
    {
        var nonChild = new TestableTreeNode();

        var removed = this.sut.RemoveChild(nonChild);

        var children = this.sut.Children;
        _ = removed.Should().BeFalse();
        _ = children.Should().BeEmpty();
        _ = nonChild.Parent.Should().BeNull();
    }

    /// <summary>
    /// Verifies that <see cref="TreeNode.AddSibling" /> properly adds the node
    /// as a sibling and updates it parent property.
    /// </summary>
    [TestMethod]
    public void AddSibling_AddsSiblingCorrectly()
    {
        var sibling = new TestableTreeNode();
        var parent = new TestableTreeNode();
        parent.AddChild(this.sut);
        this.sut.AddSibling(sibling);

        var children = parent.Children;
        _ = children.Should().HaveCount(2).And.Contain(sibling);
        _ = sibling.Parent.Should().Be(parent);
        var siblings = this.sut.Siblings;
        _ = siblings.Should().HaveCount(1).And.Contain(sibling);
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.AddSibling" /> throws a
    /// <see cref="InvalidOperationException" /> when the node does not have a
    /// parent.
    /// </summary>
    [TestMethod]
    public void AddSibling_WhenNodeHasNoParent_ThrowsInvalidOperationException()
    {
        var sibling = new TestableTreeNode();

        var act = () => this.sut.AddSibling(sibling);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.RemoveSibling" /> properly
    /// removes a sibling that exists and clears its parent property.
    /// </summary>
    [TestMethod]
    public void RemoveSibling_RemovesSiblingCorrectly()
    {
        var sibling = new TestableTreeNode();
        var parent = new TestableTreeNode();
        parent.AddChild(this.sut);
        parent.AddChild(sibling);
        _ = parent.Children.Should().HaveCount(2).And.Contain(sibling);

        var removed = this.sut.RemoveSibling(sibling);

        _ = removed.Should().BeTrue();
        _ = parent.Children.Should().ContainSingle().Which.Should().Be(this.sut);
        _ = sibling.Parent.Should().BeNull();
        _ = this.sut.Siblings.Should().BeEmpty();
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.RemoveSibling" /> returns
    /// <see langword="false" /> when the node has no parent.
    /// </summary>
    [TestMethod]
    public void RemoveSibling_WhenNodeHasNoParent_ReturnsFalse()
    {
        var sibling = new TestableTreeNode();

        var removed = this.sut.RemoveSibling(sibling);

        _ = removed.Should().BeFalse();
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.MoveTo" /> properly moves a node
    /// to its new parent and updates its parent property.
    /// </summary>
    [TestMethod]
    public void MoveTo_MovesNodeCorrectly()
    {
        var oldParent = new TestableTreeNode();
        var newParent = new TestableTreeNode();
        oldParent.AddChild(this.sut);
        _ = this.sut.Parent.Should().Be(oldParent);

        this.sut.MoveTo(newParent);

        _ = oldParent.Children.Should().BeEmpty();
        _ = newParent.Children.Should().ContainSingle().Which.Should().Be(this.sut);
        _ = this.sut.Parent.Should().Be(newParent);
    }

    /// <summary>
    /// Verifies that the <see cref="TreeNode.MoveTo" /> throws a
    /// <see cref="InvalidOperationException" /> when the node is the root.
    /// </summary>
    [TestMethod]
    public void MoveTo_WhenNodeIsRoot_ThrowsInvalidOperationException()
    {
        var newParent = new TestableTreeNode();

        var act = () => this.sut.MoveTo(newParent);

        _ = act.Should().Throw<InvalidOperationException>();
    }
}

[ExcludeFromCodeCoverage]
[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "class only using inside these unit tests")]
internal sealed class TestableTreeNode : TreeNode
{
    internal new TestableTreeNode? Parent => (TestableTreeNode?)base.Parent;

    internal new IReadOnlyCollection<TestableTreeNode> Children
        => base.Children.Cast<TestableTreeNode>().ToList().AsReadOnly();

    internal new IReadOnlyCollection<TestableTreeNode> Siblings
        => base.Siblings.Cast<TestableTreeNode>().ToList().AsReadOnly();

    internal new TestableTreeNode Root => (TestableTreeNode)base.Root;

    internal new void ClearChildren() => base.ClearChildren();

    internal new bool RemoveChild(TreeNode node) => base.RemoveChild(node);

    internal new void AddSibling(TreeNode node) => base.AddSibling(node);

    internal new bool RemoveSibling(TreeNode node) => base.RemoveSibling(node);

    internal new void MoveTo(TreeNode newParent) => base.MoveTo(newParent);

    internal new void AddChild(TreeNode node) => base.AddChild(node);
}
