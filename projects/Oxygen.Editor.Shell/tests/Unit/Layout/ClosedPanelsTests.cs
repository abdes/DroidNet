// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Docking;
using DroidNet.Docking.Workspace;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Shell.Layout;

namespace Oxygen.Editor.Shell.Unit.Tests.Layout;

/// <summary>A closed panel shown again docks back where it was: in its former dock, or where that dock was anchored.</summary>
[TestClass]
public sealed class ClosedPanelsTests : IDisposable
{
    private readonly Docker docker = new();
    private readonly Mock<IRouter> router = new();
    private readonly Mock<INavigationContext> context = new();
    private readonly List<RouteChangeItem> navigated = [];
    private readonly ClosedPanels sut;

    public ClosedPanelsTests()
    {
        var windowRoute = Mock.Of<IActiveRoute>();
        _ = this.context.SetupGet(value => value.State).Returns(Mock.Of<IRouterState>(state => state.RootNode == windowRoute));
        _ = this.router.Setup(value => value.NavigateAsync(It.IsAny<IList<RouteChangeItem>>(), It.IsAny<PartialNavigation>()))
            .Callback<IList<RouteChangeItem>, PartialNavigation>((changes, options) =>
            {
                _ = options.RelativeTo.Should().BeSameAs(windowRoute);
                this.navigated.AddRange(changes);
            })
            .Returns(Task.CompletedTask);
        this.sut = new ClosedPanels(this.router.Object, this.context.Object, this.docker);
    }

    [TestCleanup]
    public void Dispose() => this.docker.Dispose();

    /// <summary>A panel tabbed with the dock's active panel, both closed, docks where their dock was anchored.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task TabbedPanelOfAClosedDockDocksWhereTheDockWas()
    {
        this.sut.Remember([Route("content", ("bottom", null), ("h", "340")), Route("cook", ("with", "content"))]);

        _ = await this.sut.ShowAsync("cook", typeof(object)).ConfigureAwait(false);

        this.ShouldHaveAdded("cook", ("bottom", null), ("h", "340"));
    }

    /// <summary>A panel whose former dock has another of its panels shown again tabs with that panel.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task PanelTabsWithAShownPanelOfItsFormerDock()
    {
        this.sut.Remember([Route("content", ("bottom", null), ("h", "340")), Route("cook", ("with", "content"))]);
        _ = this.docker.CreateDockable<Dockable>("cook");

        _ = await this.sut.ShowAsync("content", typeof(object)).ConfigureAwait(false);

        this.ShouldHaveAdded("content", ("with", "cook"));
    }

    /// <summary>A panel closed alone from a dock whose other panels stay tabs with them again.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task PanelClosedAloneTabsWithThePanelItWasWith()
    {
        _ = this.docker.CreateDockable<Dockable>("content");
        this.sut.Remember([Route("log", ("with", "content"))]);

        _ = await this.sut.ShowAsync("log", typeof(object)).ConfigureAwait(false);

        this.ShouldHaveAdded("log", ("with", "content"));
    }

    /// <summary>A panel whose former dock mate closed later docks where that panel was.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task PanelFollowsAFormerDockMateThatClosedLater()
    {
        var content = this.docker.CreateDockable<Dockable>("content");
        this.sut.Remember([Route("log", ("with", "content"))]);
        content.Dispose();
        this.sut.Remember([Route("content", ("right", null), ("w", "300"))]);

        _ = await this.sut.ShowAsync("log", typeof(object)).ConfigureAwait(false);

        this.ShouldHaveAdded("log", ("right", null), ("w", "300"));
    }

    /// <summary>A panel the window never showed docks with the default anchor.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task PanelNeverShownDocksWithTheDefaultAnchor()
    {
        _ = await this.sut.ShowAsync("inspect", typeof(object)).ConfigureAwait(false);

        this.ShouldHaveAdded("inspect");
    }

    /// <summary>The panel is shown only once its route made its dockable.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task ShownOnlyWhenTheRouteMakesTheDockable()
    {
        _ = (await this.sut.ShowAsync("cook", typeof(object)).ConfigureAwait(false)).Should().BeFalse();

        _ = this.router.Setup(value => value.NavigateAsync(It.IsAny<IList<RouteChangeItem>>(), It.IsAny<PartialNavigation>()))
            .Callback(() => this.docker.CreateDockable<Dockable>("cook"))
            .Returns(Task.CompletedTask);

        _ = (await this.sut.ShowAsync("cook", typeof(object)).ConfigureAwait(false)).Should().BeTrue();
    }

    /// <summary>Before the window's first navigation there is no route to add the panel to.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task NothingIsShownBeforeTheWindowNavigates()
    {
        _ = this.context.SetupGet(value => value.State).Returns((IRouterState?)null);

        _ = (await this.sut.ShowAsync("cook", typeof(object)).ConfigureAwait(false)).Should().BeFalse();

        _ = this.navigated.Should().BeEmpty();
    }

    private static IActiveRoute Route(string outlet, params (string Name, string? Value)[] parameters)
    {
        var route = new Mock<IActiveRoute>();
        _ = route.SetupGet(value => value.Outlet).Returns(outlet);
        _ = route.SetupGet(value => value.Params).Returns(ParametersOf(parameters));
        return route.Object;
    }

    private static Parameters ParametersOf((string Name, string? Value)[] parameters)
    {
        var result = new Parameters();
        foreach (var (name, value) in parameters)
        {
            result.AddOrUpdate(name, value);
        }

        return result;
    }

    private void ShouldHaveAdded(string panelId, params (string Name, string? Value)[] parameters)
    {
        var change = this.navigated.Should().ContainSingle().Subject;
        _ = change.ChangeAction.Should().Be(RouteChangeAction.Add);
        _ = change.Outlet.Name.Should().Be(panelId);
        _ = change.Parameters.Should().BeEquivalentTo(ParametersOf(parameters));
    }
}
