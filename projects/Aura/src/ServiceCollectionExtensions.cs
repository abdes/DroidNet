// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.WindowManagement;
using Microsoft.Extensions.DependencyInjection;

namespace DroidNet.Aura;

/// <summary>
/// Extension methods for configuring Aura window management services.
/// </summary>
public static class ServiceCollectionExtensions
{
    /// <summary>
    /// Adds Aura window management services to the service collection.
    /// </summary>
    /// <param name="services">The service collection to configure.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// This extension registers the following services:
    /// <list type="bullet">
    /// <item><description><see cref="IWindowFactory"/> as <see cref="DefaultWindowFactory"/> (singleton)</description></item>
    /// <item><description><see cref="IWindowManagerService"/> as <see cref="WindowManagerService"/> (singleton)</description></item>
    /// </list>
    /// </remarks>
    public static IServiceCollection AddAuraWindowManagement(this IServiceCollection services)
    {
        ArgumentNullException.ThrowIfNull(services);

        _ = services.AddSingleton<IWindowFactory, DefaultWindowFactory>();
        _ = services.AddSingleton<IWindowManagerService, WindowManagerService>();

        return services;
    }

    /// <summary>
    /// Adds Aura window management services with a custom window factory.
    /// </summary>
    /// <typeparam name="TFactory">The custom window factory type.</typeparam>
    /// <param name="services">The service collection to configure.</param>
    /// <returns>The service collection for chaining.</returns>
    public static IServiceCollection AddAuraWindowManagement<TFactory>(this IServiceCollection services)
        where TFactory : class, IWindowFactory
    {
        ArgumentNullException.ThrowIfNull(services);

        _ = services.AddSingleton<IWindowFactory, TFactory>();
        _ = services.AddSingleton<IWindowManagerService, WindowManagerService>();

        return services;
    }

    /// <summary>
    /// Registers a window type as transient for creation by the window factory.
    /// </summary>
    /// <typeparam name="TWindow">The window type to register.</typeparam>
    /// <param name="services">The service collection.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// Windows should be registered as transient since a new instance is created
    /// each time the window manager requests one.
    /// </remarks>
    public static IServiceCollection AddWindow<TWindow>(this IServiceCollection services)
        where TWindow : Microsoft.UI.Xaml.Window
    {
        ArgumentNullException.ThrowIfNull(services);

        _ = services.AddTransient<TWindow>();

        return services;
    }
}
