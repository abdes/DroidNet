// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests.Detail;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using DroidNet.Routing.Detail;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Unit test cases for route recognition when building a router state from a url tree.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Router State Construction")]
[UsesVerify]
public partial class RouterStateTests
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

    [ModuleInitializer]
    public static void Init()
    {
        VerifierSettings.TreatAsString<OutletName>();
        VerifierSettings.IgnoreMember<OutletName>(o => o.IsPrimary);
        VerifierSettings.IgnoreMember<OutletName>(o => o.IsNotPrimary);
        VerifierSettings.IgnoreMembers<IRoute>(
            o => o.Outlet,
            o => o.Matcher,
            o => o.Children);
        VerifierSettings.IgnoreMember<IActiveRoute>("Root");
        VerifierSettings.IgnoreMember<IActiveRoute>("Siblings");
        VerifierSettings.IgnoreMember<IActiveRoute>("Parent");

        VerifyDiffPlex.Initialize();
    }

    [TestMethod]
    public Task VerifyConventions_Satisfied() =>
        VerifyChecks.Run();

    [TestMethod]
    [DataRow("/")]
    [DataRow("/home")]
    [DataRow("/project/(left:folders//right:assets)?selected=scenes")]
    [DataRow("/project;selected=scenes/(left:folders//right:assets)")]
    public Task RouterStateFromUrlTree_Match(string url)
    {
        var urlTree = this.parser.Parse(url);

        var state = RouterState.CreateFromUrlTree(
            urlTree,
            this.config);

        return Verify(state.RootNode.Children)
            .UseParameters(url)
            .UseDirectory("Snapshots");
    }

    public static class HomeViewModel;

    public static class ProjectViewModel;

    public static class FoldersViewModel;

    public static class AssetsViewModel;

    private static class ShellViewModel;
}
