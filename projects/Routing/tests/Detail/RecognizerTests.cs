// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Detail;
using DryIoc;
using FluentAssertions;
using ILoggerFactory = Microsoft.Extensions.Logging.ILoggerFactory;

/// <summary>
/// Unit test cases for route recognition when building a router state from a url tree.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Router State Construction")]
[UsesVerify]
public partial class RecognizerTests
{
    private readonly DefaultUrlParser parser = new();

    private readonly Routes config = new(
    [
        new Route
        {
            MatchMethod = PathMatch.Prefix,
            Path = string.Empty,
            ViewModelType = typeof(ShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Path = "home",
                    ViewModelType = typeof(HomeViewModel),
                },
                new Route
                {
                    Path = "users/:id",
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "rights",
                            ViewModelType = typeof(UserRightsViewModel),
                        },
                        new Route
                        {
                            Path = "profile",
                            ViewModelType = typeof(UserProfileViewModel),
                        },
                    ]),
                },
                new Route
                {
                    Path = "project",
                    ViewModelType = typeof(ProjectViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "folders",
                            Outlet = "left",
                            ViewModelType = typeof(FoldersViewModel),
                        },
                        new Route
                        {
                            Path = "assets",
                            Outlet = "right",
                            ViewModelType = typeof(AssetsViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    /* var loggerFactory = new NullLoggerFactory(); */
    private readonly ILoggerFactory loggerFactory = GlobalTestInitializer.Container.Resolve<ILoggerFactory>();

    [TestMethod]
    [DataRow("/home")]
    [DataRow("/")]
    [DataRow("/project/(left:folders//right:assets)?selected=scenes")]
    [DataRow("/project;foo=bar/(left:folders//right:assets)?selected=scenes")]
    [DataRow("/users;foo=not-propagated/12/profile")]
    [DataRow("/users/12/rights")]
    [DataRow("/users/14")]
    public Task Recognizer_Match(string url)
    {
        var urlTree = this.parser.Parse(url);
        var recognizer = new Recognizer(
            new DefaultUrlSerializer(new DefaultUrlParser()),
            this.config,
            this.loggerFactory);

        var state = recognizer.Recognize(urlTree);

        state.Should().NotBeNull();

        return Verify(state)
            .UseParameters(url)
            .UseDirectory("Snapshots");
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("a")]
    public void Recognizer_OutletsDiffer_Match(string outlet)
    {
        var routes = new Routes(
        [
            new Route
            {
                Path = string.Empty,
                Outlet = outlet,
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "b",
                        Outlet = "b",
                    },
                ]),
            },
        ]);

        var urlTree = this.parser.Parse("/(b:b)");
        var recognizer = new Recognizer(new DefaultUrlSerializer(new DefaultUrlParser()), routes, this.loggerFactory);

        var state = recognizer.Recognize(urlTree);

        state.Should().NotBeNull();
    }

    [TestMethod]
    public void Recognizer_OutletsDifferSegmentForPrimary_NoMatch()
    {
        var routes = new Routes(
        [
            new Route
            {
                Path = string.Empty,
                Outlet = "x",
                Children = new Routes(
                [
                    new Route
                    {
                        Path = "b",
                    },
                ]),
            },
        ]);

        var recognizer = new Recognizer(new DefaultUrlSerializer(new DefaultUrlParser()), routes, this.loggerFactory);

        var urlTree = this.parser.Parse("/b");
        var state = recognizer.Recognize(urlTree);
        state.Should().BeNull();
    }

    public static class HomeViewModel;

    public static class ProjectViewModel;

    public static class FoldersViewModel;

    public static class AssetsViewModel;

    private static class ShellViewModel;

    private static class UserRightsViewModel;

    private static class UserProfileViewModel;
}
