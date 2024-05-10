// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using static DroidNet.Routing.Utils.RelativeUrlTreeResolver;

/// <summary>
/// Contains test cases for the <see cref="ResolveUrlTreeRelativeTo" /> utility
/// function.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Relative Url Resolver")]
public class RelativeUrlTreeResolverTests
{
    private readonly DefaultUrlParser parser;
    private readonly Mock<IActiveRoute> routeMock;

    /// <summary>
    /// Initializes a new instance of the <see cref="RelativeUrlTreeResolverTests" />
    /// class.
    /// </summary>
    /// <remarks>
    /// A new instance of the test class is created for each test case.
    /// </remarks>
    public RelativeUrlTreeResolverTests()
    {
        this.parser = new DefaultUrlParser();

        this.routeMock = new Mock<IActiveRoute>();
        var mockChild1 = new Mock<IActiveRoute>();
        var mockChild2 = new Mock<IActiveRoute>();
        var mockChild3 = new Mock<IActiveRoute>();

        // Set up the UrlSegments properties
        _ = mockChild1.Setup(m => m.UrlSegments).Returns(new List<UrlSegment> { new("segment1") });
        _ = mockChild2.Setup(m => m.UrlSegments).Returns(new List<UrlSegment> { new("segment2") });
        _ = mockChild3.Setup(m => m.UrlSegments)
            .Returns(
                new List<UrlSegment>
                {
                    new("segment3"),
                    new("segment4"),
                });

        // Set up the Children properties
        _ = this.routeMock.Setup(m => m.Children).Returns(new List<IActiveRoute> { mockChild1.Object });
        _ = mockChild1.Setup(m => m.Children).Returns(new List<IActiveRoute> { mockChild2.Object });
        _ = mockChild2.Setup(m => m.Children).Returns(new List<IActiveRoute> { mockChild3.Object });

        // Set up the Parent properties
        _ = mockChild1.Setup(m => m.Parent).Returns(this.routeMock.Object);
        _ = mockChild2.Setup(m => m.Parent).Returns(mockChild1.Object);
        _ = mockChild3.Setup(m => m.Parent).Returns(mockChild2.Object);

        // Set up the Root properties
        _ = this.routeMock.Setup(m => m.Root).Returns(this.routeMock.Object);
        _ = mockChild1.Setup(m => m.Root).Returns(this.routeMock.Object);
        _ = mockChild2.Setup(m => m.Root).Returns(this.routeMock.Object);
        _ = mockChild3.Setup(m => m.Root).Returns(this.routeMock.Object);
    }

    /// <summary>
    /// Tests that an exception is thrown when trying to resolve a URL tree
    /// that is not relative.
    /// </summary>
    [TestMethod]
    public void ResolveUrlTreeRelativeTo_NotRelativeTree_Throws()
    {
        var tree = this.parser.Parse("/foo");

        var action = () => ResolveUrlTreeRelativeTo(tree, this.GetActiveRoute(1));

        _ = action.Should().Throw<InvalidOperationException>();
    }

    /// <summary>
    /// Tests that a URL tree can be resolved correctly when moving one level
    /// up.
    /// </summary>
    [TestMethod]
    public void ResolveUrlTreeRelativeTo_OnLevelUp_Works()
    {
        var tree = this.parser.Parse("../foo");

        var result = ResolveUrlTreeRelativeTo(tree, this.GetActiveRoute(3));

        _ = result.ToString().Should().Be("/segment1/segment2/segment3/foo");
    }

    /// <summary>
    /// Tests that a URL tree can be resolved correctly when moving many levels
    /// up.
    /// </summary>
    [TestMethod]
    public void ResolveUrlTreeRelativeTo_ManyLevelsUp_Works()
    {
        var tree = this.parser.Parse("../../foo");

        var result = ResolveUrlTreeRelativeTo(tree, this.GetActiveRoute(3));

        _ = result.ToString().Should().Be("/segment1/segment2/foo");
    }

    /// <summary>
    /// Tests that a URL tree can be resolved correctly when moving to the
    /// maximum level up.
    /// </summary>
    [TestMethod]
    public void ResolveUrlTreeRelativeTo_MaxLevelsUp_Works()
    {
        var tree = this.parser.Parse("../../../../foo");

        var result = ResolveUrlTreeRelativeTo(tree, this.GetActiveRoute(3));

        _ = result.ToString().Should().Be("/foo");
    }

    /// <summary>
    /// Tests that an exception is thrown when trying to resolve a URL tree
    /// that moves too many levels up.
    /// </summary>
    [TestMethod]
    public void ResolveUrlTreeRelativeTo_TooManyLevelsUp_Throws()
    {
        var tree = this.parser.Parse("../../../../../foo");

        var action = () => ResolveUrlTreeRelativeTo(tree, this.GetActiveRoute(3));

        _ = action.Should().Throw<InvalidOperationException>();
    }

    private IActiveRoute GetActiveRoute(int depth)
    {
        var route = this.routeMock.Object;
        while (depth > 0)
        {
            route = route.Children.ElementAt(0);
            depth--;
        }

        return route;
    }
}
