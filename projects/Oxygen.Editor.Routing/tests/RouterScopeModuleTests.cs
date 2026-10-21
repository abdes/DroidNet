// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Composition;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Routing.Tests;

/// <summary>
///     A scope built with <see cref="RouterScopeModule" /> has its own router and view location over a
///     root that registers the application's, and binds its local router context to its router.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Contracts")]
public sealed class RouterScopeModuleTests
{
    private readonly ScopeFactory factory = new();

    [TestMethod]
    public async Task Create_UnderTheApplicationRoot_BindsTheContextToTheScopeRouter()
    {
        // Arrange
        using var host = CreateApplicationHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var context = new LocalRouterContext(new object());

        // Act
        var window = this.CreateRouted(root, context);

        // Assert
        _ = context.LocalRouter.Should().BeSameAs(window.Resolver.Resolve<IRouter>());
        _ = window.Resolver.Resolve<ILocalRouterContext>().Should().BeSameAs(context);
        _ = window.Resolver.Resolve<INavigationContext>().Should().BeSameAs(context);
    }

    [TestMethod]
    public async Task Create_UnderTheApplicationRoot_HasItsOwnViewLocation()
    {
        // Arrange
        using var host = CreateApplicationHost();
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var window = this.CreateRouted(root, new LocalRouterContext(new object()));

        // Assert
        _ = window.Resolver.Resolve<IViewLocator>().Should().NotBeSameAs(host.Resolve<IViewLocator>());
        _ = window.Resolver.Resolve<ViewModelToView>().Should().NotBeSameAs(host.Resolve<ViewModelToView>());
    }

    [TestMethod]
    public async Task Create_PanelUnderWindow_HasItsOwnRouter()
    {
        // Arrange
        using var host = CreateApplicationHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var windowContext = new LocalRouterContext(new object());
        var panelContext = new LocalRouterContext(new object());
        var window = this.CreateRouted(root, windowContext);

        // Act
        var panel = this.CreateRouted(window, panelContext);

        // Assert
        _ = panelContext.LocalRouter.Should().BeSameAs(panel.Resolver.Resolve<IRouter>());
        _ = panelContext.LocalRouter.Should().NotBeSameAs(windowContext.LocalRouter);
    }

    /// <summary>
    ///     Creates a host container registered as the application's root is: Microsoft rules, the
    ///     application router and the application view location.
    /// </summary>
    /// <returns>The host container.</returns>
    private static Container CreateApplicationHost()
    {
        var host = new Container(DryIocAdapter.MicrosoftDependencyInjectionRules);
        host.RegisterInstance<ILoggerFactory>(NullLoggerFactory.Instance);
        host.ConfigureRouter(new Routes([]));
        host.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        host.Register<ViewModelToView>(Reuse.Singleton);
        return host;
    }

    private ScopeHandle CreateRouted(ScopeHandle parent, LocalRouterContext context)
        => this.factory.Create<IScopeLayer>(parent, [new RouterScopeModule(new Routes([]), context)]);
}
