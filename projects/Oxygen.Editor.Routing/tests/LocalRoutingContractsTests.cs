// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;

using RoutingParameters = DroidNet.Routing.Parameters;
using RoutingTarget = DroidNet.Routing.Target;

namespace Oxygen.Editor.Routing.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Contracts")]
public sealed class LocalRoutingContractsTests
{
    [TestMethod]
    public void LocalRouterContext_ShouldAlwaysUseTargetSelf()
    {
        // Arrange
        var navTarget = new object();

        // Act
        var context = new LocalRouterContext(navTarget);

        // Assert
        _ = context.NavigationTargetKey.IsSelf.Should().BeTrue();
        _ = context.NavigationTarget.Should().BeSameAs(navTarget);
    }

    [TestMethod]
    public async Task WithLocalRouting_ShouldAssignLocalRouterOnContext()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            // Act
            var router = localContext.LocalRouter;

            // Assert
            _ = router.Should().NotBeNull();
            _ = router.Should().BeSameAs(container.Resolve<IRouter>());
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalContextProvider_ShouldReturnSameContext_ForAnyTarget()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var provider = container.Resolve<IContextProvider<NavigationContext>>();

            // Act
            var c1 = provider.ContextForTarget(RoutingTarget.Main);
            var c2 = provider.ContextForTarget(new RoutingTarget { Name = "somewhere-else" });

            // Assert
            _ = c1.Should().BeSameAs(c2);
            _ = c1.Should().BeSameAs(localContext);
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalContextProvider_ShouldRaiseContextCreated_WhenLocalRouterChanges()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var provider = container.Resolve<IContextProvider<NavigationContext>>();

            INavigationContext? created = null;
            provider.ContextCreated += (_, args) => created = args.Context;

            // Act
            var replacementRouter = new Mock<IRouter>().Object;
            localContext.LocalRouter = replacementRouter;

            // Assert
            _ = created.Should().NotBeNull();
            _ = created.Should().BeSameAs(localContext);
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalContextProvider_Dispose_ShouldRaiseContextDestroyed()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var provider = container.Resolve<IContextProvider<NavigationContext>>();

            INavigationContext? destroyed = null;
            provider.ContextDestroyed += (_, args) => destroyed = args.Context;

            // Act
            (provider as IDisposable)!.Dispose();

            // Assert
            _ = destroyed.Should().NotBeNull();
            _ = destroyed.Should().BeSameAs(localContext);
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalRouteActivator_ShouldLoadIntoRootViewModel_WhenNoParentViewModelExists()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var activator = container.Resolve<IRouteActivator>();

            var viewModel = new object();
            var outlet = new OutletName { Name = "primary" };

            // parent has no VM, and there is no VM further up
            var parent = new TestActiveRoute(
                config: new Route { Path = "parent", ViewModelType = null, Outlet = OutletName.Primary },
                outlet: OutletName.Primary,
                viewModel: null,
                parent: null);

            var route = new TestActiveRoute(
                config: new Route { Path = "child", ViewModelType = typeof(object), Outlet = outlet },
                outlet: outlet,
                viewModel: viewModel,
                parent: parent);

            // Act
            var ok = await activator.ActivateRouteAsync(route, localContext).ConfigureAwait(true);

            // Assert
            _ = ok.Should().BeTrue();
            _ = rootContainer.Calls.Should().ContainSingle();
            _ = rootContainer.Calls[0].ViewModel.Should().BeSameAs(viewModel);
            _ = rootContainer.Calls[0].Outlet.Should().Be(outlet);
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalRouteActivator_ShouldLoadIntoNearestParentOutletContainer()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var parentContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var activator = container.Resolve<IRouteActivator>();

            var viewModel = new object();
            var outlet = new OutletName { Name = "side" };

            var parentWithContainer = new TestActiveRoute(
                config: new Route { Path = "host", ViewModelType = typeof(object), Outlet = OutletName.Primary },
                outlet: OutletName.Primary,
                viewModel: parentContainer,
                parent: null);

            var intermediateWithoutVm = new TestActiveRoute(
                config: new Route { Path = "intermediate", ViewModelType = null, Outlet = OutletName.Primary },
                outlet: OutletName.Primary,
                viewModel: null,
                parent: parentWithContainer);

            var route = new TestActiveRoute(
                config: new Route { Path = "leaf", ViewModelType = typeof(object), Outlet = outlet },
                outlet: outlet,
                viewModel: viewModel,
                parent: intermediateWithoutVm);

            // Act
            var ok = await activator.ActivateRouteAsync(route, localContext).ConfigureAwait(true);

            // Assert
            _ = ok.Should().BeTrue();
            _ = parentContainer.Calls.Should().ContainSingle();
            _ = parentContainer.Calls[0].ViewModel.Should().BeSameAs(viewModel);
            _ = parentContainer.Calls[0].Outlet.Should().Be(outlet);
            _ = rootContainer.Calls.Should().BeEmpty();
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalRouteActivator_ShouldSucceedWithoutLoading_WhenRouteHasNoViewModel()
    {
        // Arrange
        var rootContainer = new TestOutletContainer();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var activator = container.Resolve<IRouteActivator>();

            var route = new TestActiveRoute(
                config: new Route { Path = "leaf", ViewModelType = null, Outlet = OutletName.Primary },
                outlet: OutletName.Primary,
                viewModel: null,
                parent: null);

            // Act
            var ok = await activator.ActivateRouteAsync(route, localContext).ConfigureAwait(true);

            // Assert
            _ = ok.Should().BeTrue();
            _ = rootContainer.Calls.Should().BeEmpty();
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    [TestMethod]
    public async Task LocalRouteActivator_ShouldFail_WhenEffectiveParentIsNotAnOutletContainer()
    {
        // Arrange
        var rootContainer = new object();
        var localContext = new LocalRouterContext(target: new object())
        {
            RootViewModel = rootContainer,
        };

        var container = CreateLocalContainer(localContext);
        try
        {
            var activator = container.Resolve<IRouteActivator>();

            var route = new TestActiveRoute(
                config: new Route { Path = "leaf", ViewModelType = typeof(object), Outlet = OutletName.Primary },
                outlet: OutletName.Primary,
                viewModel: new object(),
                parent: null);

            // Act
            var ok = await activator.ActivateRouteAsync(route, localContext).ConfigureAwait(true);

            // Assert
            _ = ok.Should().BeFalse();
        }
        finally
        {
            await container.DisposeAsync().ConfigureAwait(true);
        }
    }

    private static Container CreateLocalContainer(LocalRouterContext localContext)
    {
        var container = new Container();
        container.RegisterInstance<ILoggerFactory>(NullLoggerFactory.Instance);

        _ = container.WithLocalRouting(
            routesConfig: new Routes([]),
            localRouterContext: localContext);

        return container;
    }

    private sealed record OutletLoadCall(object ViewModel, OutletName? Outlet);

    private sealed class TestOutletContainer : IOutletContainer
    {
        public List<OutletLoadCall> Calls { get; } = [];

        public void LoadContent(object viewModel, OutletName? outletName = null)
            => this.Calls.Add(new OutletLoadCall(viewModel, outletName));
    }

    private sealed class TestUrlSegment(string path) : IUrlSegment
    {
        public IParameters Parameters { get; } = new RoutingParameters();

        public string Path { get; } = path;
    }

    private sealed class TestActiveRoute : IActiveRoute
    {
        private readonly List<IActiveRoute> children = [];

        public TestActiveRoute(IRoute config, OutletName outlet, object? viewModel, TestActiveRoute? parent)
        {
            this.Config = config;
            this.Outlet = outlet;
            this.ViewModel = viewModel;
            this.Parent = parent;

            parent?.children.Add(this);

            this.Params = new RoutingParameters();
            this.QueryParams = new RoutingParameters();
            this.Segments = new List<IUrlSegment> { new TestUrlSegment("seg") }.AsReadOnly();
        }

        public IReadOnlyList<IUrlSegment> Segments { get; }

        public IParameters Params { get; }

        public IParameters QueryParams { get; }

        public OutletName Outlet { get; }

        public object? ViewModel { get; }

        public IRoute Config { get; }

        public IActiveRoute Root => this.Parent is null ? this : this.Parent.Root;

        public IActiveRoute? Parent { get; }

        public IReadOnlyCollection<IActiveRoute> Children => this.children.AsReadOnly();

        public IReadOnlyCollection<IActiveRoute> Siblings
            => this.Parent is null
                ? Array.Empty<IActiveRoute>()
                : this.Parent.Children.Where(r => !ReferenceEquals(r, this)).ToList().AsReadOnly();

        public void AddChild(IActiveRoute route) => throw new NotSupportedException();

        public bool RemoveChild(IActiveRoute route) => throw new NotSupportedException();

        public void AddSibling(IActiveRoute route) => throw new NotSupportedException();

        public bool RemoveSibling(IActiveRoute route) => throw new NotSupportedException();

        public void MoveTo(IActiveRoute parent) => throw new NotSupportedException();

        public void ClearChildren() => throw new NotSupportedException();

        public override string ToString() => this.Config.ToString() ?? string.Empty;
    }
}
