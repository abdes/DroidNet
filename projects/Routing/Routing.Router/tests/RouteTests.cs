// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Moq;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Contains unit test cases for the <see cref="Route" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Router Config")]
public class RouteTests
{
    /// <summary>Initializes a new instance of the <see cref="RouteTests" /> class.</summary>
    /// <remarks>A new instance of the test class is created for each test case.</remarks>
    public RouteTests()
    {
        this.Segments = [];
        this.Group = new UrlSegmentGroup(this.Segments);
    }

    private List<IUrlSegment> Segments { get; }

    private UrlSegmentGroup Group { get; }

    [TestMethod]
    public void Matcher_WhenNotSet_DefaultMatcherIsUsed()
    {
        var route = new Route { Path = "test" };
        this.Segments.Add(new UrlSegment("test"));
        var defaultResult = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        var result = route.Matcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.Should().BeEquivalentTo(defaultResult);
    }

    [TestMethod]
    public void Matcher_WhenSet_IsUsedForMatching()
    {
        var matcherMock = new Mock<IRoute.PathMatcher>();
        var route = new Route
        {
            Path = "test",
            Matcher = matcherMock.Object,
        };
        this.Segments.Add(new UrlSegment("test"));

        _ = route.Matcher(this.Segments.AsReadOnly(), this.Group, route);

        matcherMock.Verify(m => m(this.Segments.AsReadOnly(), this.Group, route), Times.Once);
    }

    [TestMethod]
    public void ToString_NoChildren()
    {
        var route = new Route
        {
            Outlet = "out",
            Path = "test/full",
            MatchMethod = PathMatch.Full,
        };

        var result = route.ToString();

        _ = result.Should().Be("out:test/full|F");
    }

    [TestMethod]
    public void ToString_WithChildren()
    {
        var route = new Route
        {
            Path = "test",
            Children = new Routes([new() { Path = "child" }]),
        };

        var result = route.ToString();

        _ = result.Should().Be("<pri>:test|P{<pri>:child|P}");
    }

    [TestMethod]
    public void ToString_CustomMatcher()
    {
        var route = new Route
        {
            Matcher = (_, _, _) => new Route.NoMatch(),
        };

        var result = route.ToString();

        _ = result.Should().Be("<pri>:<matcher>");
    }
}
