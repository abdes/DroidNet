// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DryIoc;

/// <summary>
/// Provides extension methods for the DryIoc <see cref="IContainer" /> class to configure a <see cref="Router" />.
/// </summary>
public static class HostBuilderExtensions
{
    /// <summary>
    /// Configures a <see cref="Router" /> for the DryIoc <see cref="IContainer" /> using the provided <see cref="Routes" />
    /// <paramref name="config" />.
    /// </summary>
    /// <param name="container">
    /// The DryIoc <see cref="IContainer" />instance to which to add service registrations for the Router module.
    /// </param>
    /// <param name="config">The <see cref="Routes" /> configuration.</param>
    public static void ConfigureRouter(this IContainer container, Routes config)
    {
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<ViewModelToView>(Reuse.Singleton);
        container.Register<IUrlSerializer, DefaultUrlSerializer>(Reuse.Singleton);
        container.Register<IUrlParser, DefaultUrlParser>(Reuse.Singleton);
        container.Register<IRouteActivator, WindowRouteActivator>(Reuse.Singleton);
        var contextProvider = new WindowContextProvider(container);
        container.RegisterInstance<IContextProvider>(contextProvider);
        container.RegisterInstance<IContextProvider<NavigationContext>>(contextProvider);
        /* Configure the router */
        container.RegisterInstance<IRoutes>(config);
        container.Register<IRouterStateManager, RouterStateManager>(Reuse.Singleton);
        container.Register<RouterContextManager>(Reuse.Singleton);
        container.Register<IRouter, Router>(Reuse.Singleton);
    }
}
