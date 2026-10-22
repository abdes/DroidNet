// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Docking;
using DroidNet.Docking.Workspace;
using DroidNet.Routing;
using Moq;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;

namespace Oxygen.Editor.Shell.Unit.Tests.Layout;

/// <summary>Showing a panel brings it forward, and a closed panel comes back through its route with its view model.</summary>
[TestClass]
public sealed class DockingPlacementServiceTests : IDisposable
{
    private readonly Docker docker = new();
    private readonly Mock<IRouter> router = new();
    private readonly List<RouteChangeItem> navigated = [];
    private readonly CookingModel retained = new();
    private readonly DockingPlacementService sut;

    public DockingPlacementServiceTests()
    {
        var context = Mock.Of<INavigationContext>(value => value.State == Mock.Of<IRouterState>(state => state.RootNode == Mock.Of<IActiveRoute>()));

        // As the window's routing does: the added route loads its view model into a new dockable, docked by its parameters.
        _ = this.router.Setup(value => value.NavigateAsync(It.IsAny<IList<RouteChangeItem>>(), It.IsAny<PartialNavigation>()))
            .Callback<IList<RouteChangeItem>, PartialNavigation>((changes, _) =>
            {
                this.navigated.AddRange(changes);
                var dockable = this.docker.CreateDockable<Dockable>(changes[0].Outlet.Name);
                dockable.ViewModel = this.retained;
                this.DockMinimized(dockable);
            })
            .Returns(Task.CompletedTask);
        var contributions = new ContributionRegistry([new PanelDescriptor("cook", "Cooking", typeof(CookingModel))], []);
        this.sut = new DockingPlacementService(Mock.Of<IDocumentArea>(), this.docker, contributions, new ClosedPanels(this.router.Object, context, this.docker));
    }

    [TestCleanup]
    public void Dispose() => this.docker.Dispose();

    /// <summary>A closed panel, such as Cooking when a cook is asked for, is docked again with the view model the window kept.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task ClosedPanelComesBackThroughItsRoute()
    {
        var shown = await this.sut.ShowPanelAsync("cook").ConfigureAwait(false);

        _ = shown.Should().BeTrue();
        var change = this.navigated.Should().ContainSingle().Subject;
        _ = change.ChangeAction.Should().Be(RouteChangeAction.Add);
        _ = change.ViewModelType.Should().Be<CookingModel>();
        var dockable = this.docker.FindDockable("cook")!;
        _ = dockable.ViewModel.Should().BeSameAs(this.retained);
        _ = dockable.IsActive.Should().BeTrue();
        _ = dockable.Owner!.State.Should().Be(DockingState.Pinned);
    }

    /// <summary>A shown panel is brought forward without navigating.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task ShownPanelIsBroughtForwardWithoutNavigating()
    {
        this.DockMinimized(this.docker.CreateDockable<Dockable>("cook"));

        _ = (await this.sut.ShowPanelAsync("cook").ConfigureAwait(false)).Should().BeTrue();

        _ = this.navigated.Should().BeEmpty();
        _ = this.docker.FindDockable("cook")!.Owner!.State.Should().Be(DockingState.Pinned);
    }

    /// <summary>A panel no feature contributes is not shown.</summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public async Task UncontributedPanelIsNotShown()
    {
        _ = (await this.sut.ShowPanelAsync("unknown").ConfigureAwait(false)).Should().BeFalse();

        _ = this.navigated.Should().BeEmpty();
    }

    private void DockMinimized(Dockable dockable)
    {
        var dock = this.docker.CreateDock<ToolDock>();
        dock.AdoptDockable(dockable);
        this.docker.Dock(dock, new AnchorRight(), minimized: true);
    }

    private sealed class CookingModel;
}
