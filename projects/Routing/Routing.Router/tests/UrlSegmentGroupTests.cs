// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Moq;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Unit test cases for the <see cref="UrlSegmentGroup" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("URL Tree")]
public class UrlSegmentGroupTests
{
    /// <summary>
    /// Test the Serialize method with a single segment and one child.
    /// </summary>
    [TestMethod]
    public void AddChild_SetsParentForChild()
    {
        // Arrange
        var segment = MockUrlSegment("foo");
        var group = new UrlSegmentGroup([segment]);
        var child = new UrlSegmentGroup([MockUrlSegment("qux")]);

        // Act
        group.AddChild("baz", child);

        // Assert
        _ = child.Parent.Should().Be(group);
    }

    /// <summary>
    /// Test that the SortedChildren property returns all children in their
    /// original order if no child is primary.
    /// </summary>
    [TestMethod]
    public void SortedChildren_NoPrimary_ReturnsChildren()
    {
        var group = new UrlSegmentGroup(
            [],
            new Dictionary<OutletName, IUrlSegmentGroup>
            {
                { "a", new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
                { "b", new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
                { "c", new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
            });

        var children = group.SortedChildren;
        var keys = children.Select(c => c.Key).ToList();

        _ = keys.Should().ContainInConsecutiveOrder("a", "b", "c");
    }

    /// <summary>
    /// Test that the SortedChildren property returns the primary outlet child
    /// first.
    /// </summary>
    [TestMethod]
    public void SortedChildren_Primary_ReturnsPrimaryFirst()
    {
        var group = new UrlSegmentGroup(
            [],
            new Dictionary<OutletName, IUrlSegmentGroup>
            {
                { "a", new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
                { OutletName.Primary, new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
                { "b", new UrlSegmentGroup([], new Dictionary<OutletName, IUrlSegmentGroup>()) },
            });

        var children = group.SortedChildren;
        var keys = children.Select(c => c.Key).ToList();

        _ = keys.Should().ContainInConsecutiveOrder(OutletName.Primary, "a", "b");
    }

    /// <summary>
    /// Test the Serialize method with a single segment and no children.
    /// </summary>
    [TestMethod]
    public void Serialize_NoSegmentNoChildren_ReturnsEmpty()
    {
        // Arrange
        var group = new UrlSegmentGroup([]);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be(string.Empty);
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with a single segment and no children.
    /// </summary>
    [TestMethod]
    public void Serialize_SingleSegmentNoChildren_ReturnsSegmentValue()
    {
        // Arrange
        var segment = MockUrlSegment("foo");
        var group = new UrlSegmentGroup([segment]);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("foo");
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with multiple segments and no children.
    /// </summary>
    [TestMethod]
    public void Serialize_MultipleSegmentsNoChildren_ReturnsSlashSeparatedSegments()
    {
        // Arrange
        var segment1 = MockUrlSegment("foo");
        var segment2 = MockUrlSegment("bar");
        var group = new UrlSegmentGroup([segment1, segment2]);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("foo/bar");
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with a single segment and one child.
    /// </summary>
    [TestMethod]
    public void Serialize_SingleSegmentOneChild_ReturnsSegmentValueWithChildValue()
    {
        // Arrange
        var segment = MockUrlSegment("foo");
        var group = new UrlSegmentGroup([segment]);
        var child = new UrlSegmentGroup([MockUrlSegment("qux")]);
        group.AddChild("baz", child);

        // Act
        var result = group.ToString();

        // Assert
        _ = group.Children.Count.Should().Be(1);
        _ = result.Should().Be("foo(baz:qux)");
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with multiple segments and multiple children.
    /// </summary>
    [TestMethod]
    public void Serialize_MultipleSegmentsMultipleChildren_ReturnsSegmentsValueWithChildrenValue()
    {
        // Arrange
        var segment1 = MockUrlSegment("foo");
        var segment2 = MockUrlSegment("bar");
        var group = new UrlSegmentGroup([segment1, segment2]);
        var child1 = new UrlSegmentGroup([MockUrlSegment("qux")]);
        var child2 = new UrlSegmentGroup([MockUrlSegment("grault"), MockUrlSegment("garply")]);
        group.AddChild("baz", child1);
        group.AddChild("corge", child2);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("foo/bar(baz:qux//corge:grault/garply)");
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with multiple segments and multiple children.
    /// </summary>
    [TestMethod]
    public void Serialize_OneChildPrimaryOutlet_ReturnsSegmentsAndPrimaryChild()
    {
        // Arrange
        var segment1 = MockUrlSegment("foo");
        var segment2 = MockUrlSegment("bar");
        var group = new UrlSegmentGroup([segment1, segment2]);
        var child = new UrlSegmentGroup([MockUrlSegment("qux")]);
        group.AddChild(OutletName.Primary, child);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("foo/bar/(qux)");
        _ = result.Should().Be(group.ToString());
    }

    /// <summary>
    /// Test the Serialize method with multiple segments and no children.
    /// </summary>
    [TestMethod]
    public void Serialize_NoSegmentsOnePrimaryChild_ReturnsChildValueNoParenthesis()
    {
        // Arrange
        var group = new UrlSegmentGroup([]);
        var child = new UrlSegmentGroup([MockUrlSegment("foo")]);
        group.AddChild(OutletName.Primary, child);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("foo");
    }

    /// <summary>
    /// Test the Serialize method with a single segment and one child.
    /// </summary>
    [TestMethod]
    public void Serialize_NoSegmentsNoPrimaryChild_ReturnsChildrenValueParenthesis()
    {
        // Arrange
        var group = new UrlSegmentGroup([]);
        var child1 = new UrlSegmentGroup([MockUrlSegment("foo")]);
        group.AddChild("left", child1);
        var child2 = new UrlSegmentGroup([MockUrlSegment("bar")]);
        group.AddChild("right", child2);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("(left:foo//right:bar)");
    }

    /// <summary>
    /// Test the Serialize method with a single segment and one child.
    /// </summary>
    [TestMethod]
    public void Serialize_NoSegmentsPrimaryAndChildren_ReturnsPrimaryThenChildrenValueParenthesis()
    {
        // Arrange
        var group = new UrlSegmentGroup([]);
        var main = new UrlSegmentGroup([MockUrlSegment("main")]);
        group.AddChild(OutletName.Primary, main);
        var child1 = new UrlSegmentGroup([MockUrlSegment("foo")]);
        group.AddChild("left", child1);
        var child2 = new UrlSegmentGroup([MockUrlSegment("bar")]);
        group.AddChild("right", child2);

        // Act
        var result = group.ToString();

        // Assert
        _ = result.Should().Be("main(left:foo//right:bar)");
    }

    private static IUrlSegment MockUrlSegment(string value)
    {
        var mock = new Mock<IUrlSegment>();
        _ = mock.Setup(m => m.ToString()).Returns(value);
        return mock.Object;
    }
}
