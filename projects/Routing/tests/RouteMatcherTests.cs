// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="Route" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Route Matching")]
public class RouteMatcherTests
{
    /// <summary>Initializes a new instance of the <see cref="RouteMatcherTests" /> class.</summary>
    /// <remarks>A new instance of the test class is created for each test case.</remarks>
    public RouteMatcherTests()
    {
        this.Segments = [];
        this.Group = new UrlSegmentGroup(this.Segments);
    }

    private List<IUrlSegment> Segments { get; }

    private UrlSegmentGroup Group { get; }

    /// <summary>
    /// Tests that the DefaultMatcher returns no match when called with empty
    /// segments.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_EmptySegments_NoMatch()
    {
        var route = new Route { Path = "test" };

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.Consumed.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns no match when the match method is
    /// full but there are extra segments.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_FullMatchButExtraSegments_NoMatch()
    {
        var route = new Route
        {
            Path = "test",
            MatchMethod = PathMatch.Full,
        };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("extra"),
        ]);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.Consumed.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns no match when the match method is
    /// full and the group has children.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_FullMatchAndGroupHasChildren_NoMatch()
    {
        var route = new Route
        {
            Path = "test",
            MatchMethod = PathMatch.Full,
        };
        this.Segments.Add(new UrlSegment("test"));
        var childSegments = new ReadOnlyCollection<UrlSegment>([new UrlSegment("child")]);
        var childGroup = new UrlSegmentGroup(childSegments);
        this.Group.AddChild("child", childGroup);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.Consumed.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that the DefaultMatcher uses prefix match when the path match is
    /// not set.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_WhenPathMatchNotSet_UsesPrefixMatch()
    {
        var routeDefault = new Route { Path = "test" };
        var routePrefix = new Route { Path = "test" };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("extra"),
        ]);

        var resultDefault = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, routeDefault);
        var resultPrefix = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, routePrefix);

        _ = resultDefault.Should().BeEquivalentTo(resultPrefix);
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns a match and consumes all segments
    /// when the match method is full and the segments match the path.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_FullMatchWithMatchingSegments_MatchAndConsumesALl()
    {
        var route = new Route { Path = "test/extra" };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("extra"),
        ]);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeTrue();
        _ = result.Consumed.Should().BeEquivalentTo(this.Segments.AsReadOnly());
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns no match when called with
    /// non-matching segments.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_WithNonMatchingSegments_NoMatch()
    {
        var route = new Route { Path = "test" };
        this.Segments.Add(new UrlSegment("different"));

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.Consumed.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns a match and updates positional
    /// parameters when called with a parameter segment.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_WithParameterSegment_MatchWithPositionalParams()
    {
        var route = new Route { Path = ":param" };
        this.Segments.Add(new UrlSegment("test"));

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeTrue();
        _ = result.PositionalParams.Should()
            .ContainKey("param")
            .WhoseValue.Should()
            .Match<UrlSegment>(p => p.Path == "test");
    }

    /// <summary>
    /// Tests that the DefaultMatcher updates the consumed segments correctly
    /// when the match fails.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_WhenMatchFails_ConsumedSegmentsAreUpdated()
    {
        var route = new Route { Path = "test/extra" };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("different"),
        ]);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.Consumed.Should().HaveCount(1).And.Contain(s => s.Path == "test");
    }

    /// <summary>
    /// Tests that the DefaultMatcher always returns an empty positional
    /// parameters dictionary when the match fails.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_WhenMatchFails_PositionalParamsIsAlwaysEmpty()
    {
        var route = new Route { Path = ":param/extra" };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("different"),
        ]);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeFalse();
        _ = result.PositionalParams.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that the DefaultMatcher returns a match when the match method is
    /// prefix and the segments match the prefix of the path.
    /// </summary>
    [TestMethod]
    public void DefaultMatcher_PrefixMatchAndSegmentsMatchPrefix_Match()
    {
        var route = new Route
        {
            Path = "test",
            MatchMethod = PathMatch.Prefix,
        };
        this.Segments.AddRange(
        [
            new UrlSegment("test"),
            new UrlSegment("extra"),
        ]);

        var result = Route.DefaultMatcher(this.Segments.AsReadOnly(), this.Group, route);

        _ = result.IsMatch.Should().BeTrue();
        _ = result.Consumed.Should().HaveCount(1).And.Contain(s => s.Path == "test");
    }
}
