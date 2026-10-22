// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Routing.Events;
using DryIoc;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Contains test cases for the <see cref="Router" /> class: the change sets of partial navigations, and the view
/// models routing lets go of.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Router))]
public sealed class RouterTests : IDisposable
{
    // Panels grouped under an empty-path route, as a workspace declares them, shown by the URL `/(owned:owned//kept:kept)`.
    private static readonly Routes Config = new(
    [
        new Route
        {
            Path = string.Empty,
            Children = new Routes(
            [
                new Route { Outlet = "owned", Path = "owned", ViewModelType = typeof(OwnedViewModel) },
                new Route { Outlet = "kept", Path = "kept", ViewModelType = typeof(KeptViewModel) },
            ]),
        },
    ]);

    private readonly Container container = new();
    private readonly SingleContextProvider contexts = new();
    private readonly RouterContextManager contextManager;
    private readonly Router router;

    public RouterTests()
    {
        this.container.Register<OwnedViewModel>(Reuse.Transient, setup: Setup.With(allowDisposableTransient: true));
        this.container.Register<KeptViewModel>(Reuse.Singleton);
        this.contextManager = new RouterContextManager(this.contexts);
        this.router = new Router(
            this.container,
            Config,
            new RouterStateManager(),
            this.contextManager,
            new NoOpRouteActivator(),
            this.contexts,
            new DefaultUrlSerializer(new DefaultUrlParser()),
            loggerFactory: null);
    }

    private IActiveRoute Root => this.contexts.Context.State!.RootNode;

    [TestCleanup]
    public void Dispose()
    {
        this.router.Dispose();
        this.contextManager.Dispose();
        this.container.Dispose();
    }

    [TestMethod]
    public async Task Delete_DisposesTheRouteViewModel_AfterTheNavigationEnds()
    {
        // Arrange
        await this.router.NavigateAsync("/(owned:owned//kept:kept)").ConfigureAwait(false);
        var owned = (OwnedViewModel)this.RouteIn("owned").ViewModel!;
        bool? disposedAtEnd = null;
        using var ends = this.router.Events.OfType<NavigationEnd>().Skip(1).Subscribe(_ => disposedAtEnd = owned.IsDisposed);

        // Act
        await this.router.NavigateAsync([Delete("owned")], new PartialNavigation { RelativeTo = this.Root }).ConfigureAwait(false);

        // Assert
        _ = disposedAtEnd.Should().BeFalse("the deleted route's view goes away with the navigation's end");
        _ = owned.IsDisposed.Should().BeTrue();
        _ = this.Root.Children.Should().NotContain(route => route.Outlet.Name == "owned");
    }

    [TestMethod]
    public async Task Delete_LeavesARetainedViewModelToItsOwner()
    {
        // Arrange
        await this.router.NavigateAsync("/(owned:owned//kept:kept)").ConfigureAwait(false);
        var kept = this.container.Resolve<KeptViewModel>();

        // Act
        await this.router.NavigateAsync([Delete("kept")], new PartialNavigation { RelativeTo = this.Root }).ConfigureAwait(false);

        // Assert
        _ = kept.IsDisposed.Should().BeFalse();
        _ = this.Root.Children.Should().NotContain(route => route.Outlet.Name == "kept");
    }

    [TestMethod]
    public async Task Add_InAnEmptyPathGroup_ActivatesTheRouteWithItsParameters()
    {
        // Arrange
        await this.router.NavigateAsync("/(owned:owned//kept:kept)").ConfigureAwait(false);
        var kept = this.container.Resolve<KeptViewModel>();
        await this.router.NavigateAsync([Delete("kept")], new PartialNavigation { RelativeTo = this.Root }).ConfigureAwait(false);
        var parameters = new Parameters();
        parameters.AddOrUpdate("right", value: null);
        parameters.AddOrUpdate("w", "300");

        // Act
        await this.router.NavigateAsync(
            [
                new RouteChangeItem
                {
                    ChangeAction = RouteChangeAction.Add,
                    Outlet = "kept",
                    ViewModelType = typeof(KeptViewModel),
                    Parameters = parameters,
                },
            ],
            new PartialNavigation { RelativeTo = this.Root }).ConfigureAwait(false);

        // Assert
        var added = this.RouteIn("kept");
        _ = added.ViewModel.Should().BeSameAs(kept, "the container keeps the retained view model for the next route to show it");
        _ = added.Params.Should().BeEquivalentTo(parameters);
        _ = this.contexts.Context.State!.Url.Should().Contain("kept:kept;right;w=300");
    }

    [TestMethod]
    public async Task Add_ForAnUnknownOutlet_FailsTheNavigation()
    {
        // Arrange
        await this.router.NavigateAsync("/(owned:owned)").ConfigureAwait(false);
        var errors = 0;
        using var failures = this.router.Events.OfType<NavigationError>().Subscribe(_ => errors++);

        // Act
        await this.router.NavigateAsync(
            [new RouteChangeItem { ChangeAction = RouteChangeAction.Add, Outlet = "missing", Parameters = new Parameters() }],
            new PartialNavigation { RelativeTo = this.Root }).ConfigureAwait(false);

        // Assert
        _ = errors.Should().Be(1);
        _ = this.Root.Children.Should().ContainSingle(route => route.Outlet.Name == "owned");
    }

    [TestMethod]
    public async Task ContextDestroyed_DisposesTheViewModelsItDoesNotRetain()
    {
        // Arrange
        await this.router.NavigateAsync("/(owned:owned//kept:kept)").ConfigureAwait(false);
        var owned = (OwnedViewModel)this.RouteIn("owned").ViewModel!;
        var kept = this.container.Resolve<KeptViewModel>();

        // Act
        this.contexts.Destroy();

        // Assert
        _ = owned.IsDisposed.Should().BeTrue();
        _ = kept.IsDisposed.Should().BeFalse();
    }

    private static RouteChangeItem Delete(string outlet) => new() { ChangeAction = RouteChangeAction.Delete, Outlet = outlet };

    private IActiveRoute RouteIn(string outlet) => this.Root.Children.Single(route => route.Outlet.Name == outlet);

    private class DisposableViewModel : IDisposable
    {
        public bool IsDisposed { get; private set; }

        public void Dispose() => this.IsDisposed = true;
    }

    private sealed class OwnedViewModel : DisposableViewModel;

    [RetainedViewModel]
    private sealed class KeptViewModel : DisposableViewModel;

    private sealed class NoOpRouteActivator : AbstractRouteActivator
    {
        protected override void DoActivateRoute(IActiveRoute route, INavigationContext context)
        {
        }
    }

    // One navigation context, the main one, which the test destroys.
    private sealed class SingleContextProvider : IContextProvider<NavigationContext>
    {
        public event EventHandler<ContextEventArgs>? ContextChanged;

        public event EventHandler<ContextEventArgs>? ContextCreated;

        public event EventHandler<ContextEventArgs>? ContextDestroyed;

        public NavigationContext Context { get; } = new(Target.Main, new object());

        public NavigationContext ContextForTarget(Target target, NavigationContext? currentContext = null, NavigationOptions? options = null)
        {
            this.ContextCreated?.Invoke(this, new ContextEventArgs(this.Context));
            return this.Context;
        }

        INavigationContext IContextProvider.ContextForTarget(Target target, INavigationContext? currentContext, NavigationOptions? options)
            => this.ContextForTarget(target, currentContext as NavigationContext, options);

        public void ActivateContext(NavigationContext context) => this.ContextChanged?.Invoke(this, new ContextEventArgs(context));

        void IContextProvider.ActivateContext(INavigationContext context) => this.ActivateContext((NavigationContext)context);

        public void Destroy() => this.ContextDestroyed?.Invoke(this, new ContextEventArgs(this.Context));
    }
}
