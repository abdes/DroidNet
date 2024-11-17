// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DryIoc;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// Extension method for configuring routing in a .NET application using the DryIoc container.
/// </summary>
/// <remarks>
/// <para>
/// This extension enables easy integration of the routing system into applications built with the
/// standard .NET hosting model.
/// </para>
/// <para><strong>Service Registration</strong></para>
/// <para>
/// When configuring routing, the following services are automatically registered:
/// </para>
/// <list type="bullet">
///   <item>View resolution services (<see cref="IViewLocator"/>)</item>
///   <item>Core routing services (<see cref="IRouter"/>, <see cref="IRoutes"/>)</item>
///   <item>Windowed navigation specific services (<see cref="WindowContextProvider"/>, <see cref="WindowRouteActivator"/>)</item>
/// </list>
/// </remarks>
///
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// Host.CreateDefaultBuilder()
///     .ConfigureServices((context, services) =>
///     {
///         // Register views and view models
///         services.AddTransient<ShellView>();
///         services.AddTransient<ShellViewModel>();
///     })
///     .ConfigureRouter(routes =>
///     {
///         routes.Add(new Route
///         {
///             Path = string.Empty,
///             ViewModelType = typeof(ShellViewModel),
///             Children = new Routes([
///                 new Route
///                 {
///                     Path = "dashboard",
///                     ViewModelType = typeof(DashboardViewModel)
///                 },
///                 new Route
///                 {
///                     Path = "settings",
///                     ViewModelType = typeof(SettingsViewModel)
///                 }
///             ])
///         });
///     });
/// ]]></code>
/// </example>
public static class HostBuilderExtensions
{
    /// <summary>
    /// Configures routing services in a DryIoC container.
    /// </summary>
    /// <param name="container">The DryIoC container to configure.</param>
    /// <param name="config">The routes configuration for the application.</param>
    /// <exception cref="ArgumentNullException">
    /// Thrown if <paramref name="container"/> or <paramref name="config"/> is <see langword="null"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This extension method registers all required services for the routing system with appropriate
    /// lifetimes. It sets up:
    /// </para>
    /// <list type="bullet">
    ///   <item>View resolution services (<see cref="IViewLocator"/>)</item>
    ///   <item>Core routing services (<see cref="IRouter"/>, <see cref="IRoutes"/>)</item>
    ///   <item>Windowed navigation specific services (<see cref="WindowContextProvider"/>, <see cref="WindowRouteActivator"/>)</item>
    /// </list>
    /// </remarks>
    ///
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var container = new Container();
    /// var routes = new Routes([
    ///     new Route
    ///     {
    ///         Path = string.Empty,
    ///         ViewModelType = typeof(ShellViewModel)
    ///     }
    /// ]);
    ///
    /// container.ConfigureRouter(routes);
    /// ]]></code>
    /// </example>
    public static void ConfigureRouter(this IContainer container, Routes config)
    {
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<ViewModelToView>(Reuse.Singleton);

        container.Register<IUrlSerializer, DefaultUrlSerializer>(Reuse.Singleton);
        container.Register<IUrlParser, DefaultUrlParser>(Reuse.Singleton);
        container.RegisterInstance<IRoutes>(config);
        container.Register<IRouterStateManager, RouterStateManager>(Reuse.Singleton);
        container.Register<RouterContextManager>(Reuse.Singleton);
        container.Register<IRouter, Router>(Reuse.Singleton);

        var contextProvider = new WindowContextProvider(container);
        container.RegisterInstance<IContextProvider>(contextProvider);
        container.RegisterInstance<IContextProvider<NavigationContext>>(contextProvider);
        container.Register<IRouteActivator, WindowRouteActivator>(Reuse.Singleton);
    }
}
