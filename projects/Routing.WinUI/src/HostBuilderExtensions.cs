// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using DroidNet.Routing.Converters;
using DroidNet.Routing.View;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

/// <summary>
/// Provides extension methods for the <see cref="IHostBuilder" />
/// class to configure a <see cref="Router" />.
/// </summary>
public static class HostBuilderExtensions
{
    /// <summary>
    /// Configures a <see cref="Router" /> for the
    /// <see cref="IHostBuilder" /> using the provided <see cref="Routes" />
    /// <paramref name="config" />.
    /// </summary>
    /// <param name="hostBuilder">
    /// The <see cref="IHostBuilder" />instance.
    /// </param>
    /// <param name="config">The <see cref="Routes" /> configuration.</param>
    /// <returns>The <see cref="IHostBuilder" /> instance.</returns>
    public static IHostBuilder ConfigureRouter(this IHostBuilder hostBuilder, Routes config)
    {
        _ = hostBuilder.ConfigureServices(
            (_, services) => services
                /* Configure the router support services*/
                .AddSingleton<IViewLocator, DefaultViewLocator>()
                .AddSingleton<ViewModelToView>()
                .AddSingleton<IUrlSerializer, DefaultUrlSerializer>()
                .AddSingleton<IUrlParser, DefaultUrlParser>()
                .AddSingleton<IRouteActivator, WindowRouteActivator>()
                .AddSingleton<IContextProvider, WindowContextProvider>()
                /* Configure the router */
                .AddSingleton<IRoutes>(config)
                .AddSingleton<IRouterStateManager, RouterStateManager>()
                .AddSingleton<RouterContextManager>()
                .AddSingleton<IRouter, Router>());

        return hostBuilder;
    }
}
