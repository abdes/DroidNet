// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Workspace;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.Workspace;

namespace Oxygen.Editor.WorldEditor.Routing;

/// <summary>
/// Provides extension methods for configuring local routing and MVVM support in a DryIoc container.
/// </summary>
public static class LocalRoutingExtensions
{
    /// <summary>
    /// Configures the container with MVVM support.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The configured DryIoc container.</returns>
    public static IContainer WithMvvm(this IContainer container)
    {
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<ViewModelToView>(Reuse.Singleton);

        return container;
    }

    /// <summary>
    /// Configures the container with local routing support.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <param name="routesConfig">The routes configuration.</param>
    /// <param name="localRouterContext">The local router context.</param>
    /// <returns>The configured DryIoc container.</returns>
    public static IContainer WithLocalRouting(this IContainer container, IRoutes routesConfig, LocalRouterContext localRouterContext)
    {
        container.RegisterInstance(routesConfig);
        container.RegisterInstance<ILocalRouterContext>(localRouterContext);
        container.RegisterInstance<INavigationContext>(localRouterContext);
        container.Register<IRouterStateManager, RouterStateManager>(Reuse.Singleton);
        container.Register<IRouteActivator, LocalRouteActivator>(Reuse.Singleton);
        container.Register<IUrlParser, DefaultUrlParser>(Reuse.Singleton);
        container.Register<IUrlSerializer, DefaultUrlSerializer>(Reuse.Singleton);
        container.Register<IContextProvider<NavigationContext>, LocalRouterContextProvider>(Reuse.Singleton);
        container.RegisterDelegate<IContextProvider>(r => r.Resolve<IContextProvider<NavigationContext>>());
        container.Register<RouterContextManager>(Reuse.Singleton);
        container.Register<IRouter, Router>(Reuse.Singleton);

        localRouterContext.LocalRouter = container.Resolve<IRouter>();

        return container;
    }

    /// <summary>
    /// Configures the container with docking support.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The configured DryIoc container.</returns>
    public static IContainer WithDocking(this IContainer container)
    {
        container.Register<IDocker, Docker>(Reuse.Singleton);
        container.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
        container.Register<DockingWorkspaceLayout>(Reuse.Singleton);

        return container;
    }
}
