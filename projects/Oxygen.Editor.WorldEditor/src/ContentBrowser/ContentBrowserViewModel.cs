// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using Oxygen.Editor.WorldEditor.Routing;
using IContainer = DryIoc.IContainer;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The ViewModel for the <see cref="ContentBrowserView"/> view.
/// </summary>
/// <remarks>
/// This is an <see cref="IOutletContainer"/> with two outlets, the "left" outlet for the left pane, and the "right" outlet for
/// the right pane.
/// </remarks>
public sealed partial class ContentBrowserViewModel(IContainer container, IRouter parentRouter) : AbstractOutletContainer, IRoutingAware
{
    private static readonly Routes RoutesConfig = new(
    [
        new Route()
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            Children = new Routes(
            [
                new Route() { Outlet = "left", Path = "project", ViewModelType = typeof(ProjectLayoutViewModel), },
                new Route()
                {
                    Path = string.Empty,
                    Outlet = "right",
                    ViewModelType = typeof(AssetsViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "assets/tiles", Outlet = "right", ViewModelType = typeof(TilesLayoutViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    private bool isDisposed;
    private IContainer? childContainer;

    /// <summary>
    /// Gets the local ViewModel to View converter. Guaranteed to be not <see langword="null"/> when the view is loaded.
    /// </summary>
    public ViewModelToView? VmToViewConverter { get; private set; }

    /// <summary>
    /// Gets the ViewModel for the left pane.
    /// </summary>
    public object? LeftPaneViewModel => this.Outlets["left"].viewModel;

    /// <summary>
    /// Gets the ViewModel for the right pane.
    /// </summary>
    public object? RightPaneViewModel => this.Outlets["right"].viewModel;

    /// <inheritdoc/>
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.Outlets.Add("left", (nameof(this.LeftPaneViewModel), null));
        this.Outlets.Add("right", (nameof(this.RightPaneViewModel), null));

        this.childContainer = container
            .WithRegistrationsCopy()
            .WithMvvm()
            .WithLocalRouting(
                RoutesConfig,
                new LocalRouterContext(navigationContext.NavigationTarget)
                {
                    ParentRouter = parentRouter,
                    RootViewModel = this,
                });

        this.childContainer.Register<ProjectLayoutViewModel>(Reuse.Singleton);
        this.childContainer.Register<ProjectLayoutView>(Reuse.Singleton);
        this.childContainer.Register<AssetsViewModel>(Reuse.Singleton);
        this.childContainer.Register<AssetsView>(Reuse.Singleton);
        this.childContainer.Register<TilesLayoutViewModel>(Reuse.Singleton);
        this.childContainer.Register<TilesLayoutView>(Reuse.Singleton);

        // Make sure to use our own ViewModel to View converter
        this.VmToViewConverter = this.childContainer.Resolve<ViewModelToView>();

        var localRouter = this.childContainer.Resolve<IRouter>();
        await localRouter.NavigateAsync("/(left:project//right:assets/tiles)?path=Scenes&path=Media").ConfigureAwait(true);
    }

    /// <inheritdoc/>
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.isDisposed = true;
            this.childContainer?.Dispose();
        }

        base.Dispose(disposing);
    }
}
