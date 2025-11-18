// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using AwesomeAssertions;
using DroidNet.Routing.Detail;
using DroidNet.Routing.Events;
using DryIoc;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Tests covering the route activation optimizer.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Route Optimization")]
public sealed class RouterOptimizerTests : IDisposable
{
    private readonly Router router;
    private readonly MethodInfo markRoutesMethod;
    private readonly RouterContextManager contextManager;
    private readonly Container container;

    public RouterOptimizerTests()
    {
        var contextProvider = new TestContextProvider();
        this.contextManager = new RouterContextManager(contextProvider);
        this.container = new Container();

        this.router = new Router(
            this.container,
            new Routes([]),
            new RouterStateManager(),
            this.contextManager,
            new NoOpRouteActivator(),
            contextProvider,
            new TestUrlSerializer(),
            NullLoggerFactory.Instance);

        this.markRoutesMethod = typeof(Router).GetMethod(
                "MarkAlreadyActivatedRoutes",
                BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Unable to locate optimizer method for tests.");
    }

    public void Dispose()
    {
        this.router.Dispose();
        this.contextManager.Dispose();
        this.container.Dispose();
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_ReusesViewModelInstances()
    {
        // Arrange
        var previousRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel());
        var newRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel));

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoute, previousRoute);

        // Assert
        _ = newRoute.IsActivated.Should().BeTrue();
        _ = newRoute.ViewModel.Should().BeSameAs(previousRoute.ViewModel);
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_DoesNotReuseMismatchedChildren()
    {
        // Arrange
        var previousRoot = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel());
        var previousChild = CreateRoute("child", OutletName.FromString("sidebar"), typeof(TestChildViewModel), new TestChildViewModel());
        previousRoot.AddChild(previousChild);

        var newRoot = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel));
        var newChild = CreateRoute("other-child", OutletName.FromString("sidebar"), typeof(TestChildViewModel));
        newRoot.AddChild(newChild);

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoot, previousRoot);

        // Assert
        _ = newRoot.ViewModel.Should().BeSameAs(previousRoot.ViewModel);
        _ = newChild.IsActivated.Should().BeFalse();
        _ = newChild.ViewModel.Should().Be(typeof(TestChildViewModel));
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_ReusesChildViewModelsByOutlet()
    {
        // Arrange
        var previousRoot = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel());
        var previousChild = CreateRoute("child", OutletName.FromString("sidebar"), typeof(TestChildViewModel), new TestChildViewModel());
        previousRoot.AddChild(previousChild);

        var newRoot = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel));
        var newChild = CreateRoute("child", OutletName.FromString("sidebar"), typeof(TestChildViewModel));
        newRoot.AddChild(newChild);

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoot, previousRoot);

        // Assert
        var activatedChild = (ActiveRoute)newRoot.Children.Single();
        _ = activatedChild.IsActivated.Should().BeTrue();
        _ = activatedChild.ViewModel.Should().BeSameAs(previousChild.ViewModel);
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_DoesNotReuseWhenPreviousWasNotActivated()
    {
        // Arrange
        var previousRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel());
        previousRoute.IsActivated = false;
        var newRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel));

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoute, previousRoute);

        // Assert
        _ = newRoute.IsActivated.Should().BeFalse();
        _ = newRoute.ViewModel.Should().Be(typeof(TestHostViewModel));
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_DoesNotReuseWhenParametersDiffer()
    {
        // Arrange
        var previousParams = new Parameters();
        previousParams.AddOrUpdate("id", "1");
        var newParams = new Parameters();
        newParams.AddOrUpdate("id", "2");

        var previousRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel(), previousParams);
        var newRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), instance: null, newParams);

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoute, previousRoute);

        // Assert
        _ = newRoute.IsActivated.Should().BeFalse();
        _ = newRoute.ViewModel.Should().Be(typeof(TestHostViewModel));
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_ReusesChildEvenIfParentNotReusable()
    {
        // Arrange
        var previousRoot = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel());
        var previousChild = CreateRoute("child", OutletName.FromString("sidebar"), typeof(TestChildViewModel), new TestChildViewModel());
        previousRoot.AddChild(previousChild);

        var newRoot = CreateRoute("root2", OutletName.Primary, typeof(TestHostViewModel));
        var newChild = CreateRoute("child", OutletName.FromString("sidebar"), typeof(TestChildViewModel));
        newRoot.AddChild(newChild);

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoot, previousRoot);

        // Assert
        _ = newRoot.IsActivated.Should().BeFalse();
        var activatedChild = (ActiveRoute)newRoot.Children.Single();
        _ = activatedChild.IsActivated.Should().BeTrue();
        _ = activatedChild.ViewModel.Should().BeSameAs(previousChild.ViewModel);
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_DoesNotReuseWhenQueryParametersDiffer()
    {
        // Arrange
        var previousQuery = new Parameters();
        previousQuery.AddOrUpdate("lang", "en");
        var newQuery = new Parameters();
        newQuery.AddOrUpdate("lang", "fr");

        var previousRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), new TestHostViewModel(), queryParameters: previousQuery);
        var newRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel), queryParameters: newQuery);

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoute, previousRoute);

        // Assert
        _ = newRoute.IsActivated.Should().BeFalse();
        _ = newRoute.ViewModel.Should().Be(typeof(TestHostViewModel));
    }

    [TestMethod]
    public void MarkAlreadyActivatedRoutes_WithNoPreviousRoute_DoesNotActivate()
    {
        // Arrange
        var newRoute = CreateRoute("root", OutletName.Primary, typeof(TestHostViewModel));

        // Act
        this.InvokeMarkAlreadyActivatedRoutes(newRoute, previous: null);

        // Assert
        _ = newRoute.IsActivated.Should().BeFalse();
        _ = newRoute.ViewModel.Should().Be(typeof(TestHostViewModel));
    }

    private static ActiveRoute CreateRoute(
        string path,
        OutletName outlet,
        Type? viewModelType,
        object? instance = null,
        Parameters? parameters = null,
        Parameters? queryParameters = null)
        => new()
        {
            Config = new Route
            {
                Path = path,
                ViewModelType = viewModelType,
                Outlet = outlet,
                Children = new Routes([]),
            },
            Outlet = outlet,
            Params = parameters ?? [],
            QueryParams = queryParameters ?? [],
            Segments = [],
            SegmentGroup = new UrlSegmentGroup([]),
            ViewModel = instance ?? viewModelType,
            IsActivated = instance is not null,
        };

    private void InvokeMarkAlreadyActivatedRoutes(IActiveRoute current, IActiveRoute? previous)
        => this.markRoutesMethod.Invoke(this.router, [current, previous]);

    private sealed class NoOpRouteActivator : IRouteActivator
    {
        public Task<bool> ActivateRouteAsync(IActiveRoute route, INavigationContext context) => Task.FromResult(true);

        public Task<bool> ActivateRoutesRecursiveAsync(IActiveRoute root, INavigationContext context) => Task.FromResult(true);
    }

    private sealed class TestUrlSerializer : IUrlSerializer
    {
        public IUrlTree Parse(string url) => new UrlTree(new UrlSegmentGroup([]));

        public string Serialize(IUrlTree tree) => string.Empty;
    }

    private sealed class TestContextProvider : IContextProvider<NavigationContext>, IContextProvider
    {
        public event EventHandler<ContextEventArgs>? ContextChanged
        {
            add { }
            remove { }
        }

        public event EventHandler<ContextEventArgs>? ContextCreated
        {
            add { }
            remove { }
        }

        public event EventHandler<ContextEventArgs>? ContextDestroyed
        {
            add { }
            remove { }
        }

        public NavigationContext ContextForTarget(Target target, NavigationContext? currentContext = null)
        {
            _ = this;
            return currentContext ?? new NavigationContext(target, new object());
        }

        public NavigationContext ContextForTarget(Target target, NavigationContext? currentContext = null, NavigationOptions? options = null)
            => this.ContextForTarget(target, currentContext);

        INavigationContext IContextProvider.ContextForTarget(Target target, INavigationContext? currentContext, NavigationOptions? options)
            => this.ContextForTarget(target, currentContext as NavigationContext, options);

        public void ActivateContext(NavigationContext context)
        {
        }

        void IContextProvider.ActivateContext(INavigationContext context)
            => this.ActivateContext((NavigationContext)context);
    }

    private sealed class TestHostViewModel;

    private sealed class TestChildViewModel;
}
