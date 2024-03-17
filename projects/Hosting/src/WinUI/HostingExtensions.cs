// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using DroidNet.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;

/// <summary>
/// Contains helper extensions for <see cref="HostApplicationBuilder" /> to configure the WinUI service hosting.
/// </summary>
public static class HostingExtensions
{
    /// <summary>
    /// The key used to access the <see cref="HostingContext" /> instance in <see cref="IHostBuilder.Properties" />.
    /// </summary>
    public const string HostingContextKey = "UserInterfaceHostingContext";

    /// <summary>Configures the host builder for a Windows UI (WinUI) application.</summary>
    /// <typeparam name="TApplication">The concrete type for the <see cref="Application" /> class.</typeparam>
    /// <param name="builder">The host builder to which the WinUI service needs to be added.</param>
    /// <returns>The host builder for chaining calls.</returns>
    /// <exception cref="ArgumentException">When the application's type does not extend <see cref="Application" />.</exception>
    /// <remarks>
    /// <para>
    /// This method configures the host builder to support a Windows UI (WinUI) application. It sets up the necessary services,
    /// including the hosting context, user interface thread, and the hosted service for the user interface.
    /// </para>
    /// <para>
    /// It attempts to find a <see cref="HostingContext" /> instance from the host builder properties and if not available creates
    /// one and adds it as a singleton service and as a <see cref="BaseHostingContext" /> service for use by the <see cref="UserInterfaceHostedService" />.
    /// </para>
    /// <para>
    /// Upon successful completion, the dependency injector will be able to provide the single instance of the application as
    /// <typeparamref name="TApplication" /> and as <see cref="Application" /> if it is not the same type.
    /// </para>
    /// </remarks>
    public static IHostBuilder ConfigureWinUI<TApplication>(this IHostBuilder builder)
        where TApplication : Application => builder.ConfigureServices(
        (builderContext, services) =>
        {
            HostingContext hostingContext;
            if (builderContext.Properties.TryGetValue(HostingContextKey, out var hostingContextAsObject))
            {
                hostingContext = (HostingContext)hostingContextAsObject;
            }
            else
            {
                hostingContext = new HostingContext { IsLifetimeLinked = true };
                builder.Properties[HostingContextKey] = hostingContext;
            }

            _ = services
                .AddSingleton(hostingContext)
                .AddSingleton<IUserInterfaceThread, UserInterfaceThread>()
                .AddHostedService<UserInterfaceHostedService>()
                .AddSingleton<TApplication>();

            if (typeof(TApplication) != typeof(Application))
            {
                _ = services.AddSingleton<Application>(sp => sp.GetRequiredService<TApplication>());
            }
        });
}
