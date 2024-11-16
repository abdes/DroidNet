// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if false
namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

[TestClass]
[ExcludeFromCodeCoverage]
public class MatchTests
{
    [TestMethod]
    public void Test1()
    {
        List<Route> routes =
        [
            new Route
            {
                Path = ":folder",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "",
                    },
                    new Route
                    {
                        Path = ":id",
                        Children = new Routes(
                        [
                            new Route
                            {
                                Path = "messages",
                            },
                            new Route
                            {
                                Path = "messages/:id",
                            },
                        ]),
                    },
                ]),
            },

            new Route
            {
                Path = "compose",

                //Outlet = "popup",
            },

            new Route
            {
                Path = "message/:id",

                //Outlet = "popup",
            },
        ];

        List<UrlSegment> segments =
        [
            new UrlSegment("inbox"),
            new UrlSegment("33"),
            new UrlSegment("messages"),
            new UrlSegment("44"),
        ];

        var match = Router.MatchSegments(segments, routes);

        _ = match.Should().NotBeNull();
    }

    [TestMethod]
    public void BackTrack()
    {
        List<Route> routes =
        [
            new Route
            {
                Path = "a",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "b",
                    },
                ]),
            },

            new Route
            {
                Path = ":folder",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "c",
                    },
                ]),
            },
        ];

        List<UrlSegment> segments =
        [
            new UrlSegment("a"),
            new UrlSegment("c"),
        ];

        var match = Router.MatchSegments(segments, routes);

        _ = match.Should().NotBeNull();
    }

    [TestMethod]
    public void DepthFirst()
    {
        List<Route> routes =
        [
            new Route
            {
                Path = ":folder",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "b",
                    },
                ]),
            },

            new Route
            {
                Path = "a",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "b",
                    },
                ]),
            },
        ];

        List<UrlSegment> segments =
        [
            new UrlSegment("a"),
            new UrlSegment("b"),
        ];

        var match = Router.MatchSegments(segments, routes);

        _ = match.Should().NotBeNull();
        _ = match!.PositionalParams.Should().HaveCount(1).And.ContainKey("folder").WhoseValue.Should().Be(segments[0]);
    }
}
#endif
