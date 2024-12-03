// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;

namespace DroidNet.Hosting.WinUI;

/// <summary>
/// Contains helper extensions for <see cref="HostApplicationBuilder" /> to configure the WinUI
/// service hosting.
/// </summary>
public static class WinUiHostingExtensions
{
    /// <summary>
    /// Configures the host builder for a Windows UI (WinUI) application.
    /// </summary>
    /// <typeparam name="TApplication">
    /// The concrete type for the <see cref="Application" /> class.
    /// </typeparam>
    /// <param name="services">
    /// The collection of services to which the WinUI hosting services will be added.
    /// </param>
    /// <param name="isLifetimeLinked">
    /// Specifies whether the UI lifecycle and the Hosted Application lifecycle are linked or not.
    /// When <see langword="true" />, termination of the UI thread leads to termination of the
    /// Hosted Application and vice versa.
    /// </param>
    /// <exception cref="ArgumentException">
    /// Thrown when the application's type does not extend <see cref="Application" />.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method configures the host builder to support a Windows UI (WinUI) application. It sets
    /// up the necessary services, including the hosting context, user interface thread, and the
    /// hosted service for the user interface.
    /// </para>
    /// <para>
    /// The <see cref="HostingContext" /> is used to manage the lifecycle and configuration of the
    /// UI thread. It provides the necessary context, such as the dispatcher queue and the
    /// application instance, to the <see cref="UserInterfaceThread" />.
    /// The <see cref="BaseHostingContext.IsLifetimeLinked" /> property determines whether the UI
    /// thread's lifecycle is linked to the application's lifecycle. If linked, terminating the UI
    /// thread will also terminate the application and vice versa.
    /// </para>
    /// <para>
    /// Upon successful completion, the dependency injector will be able to provide the single
    /// instance of the application as
    /// <typeparamref name="TApplication" /> and as <see cref="Application" /> if it's not the same
    /// type.
    /// </para>
    /// </remarks>
    public static void ConfigureWinUI<TApplication>(this IServiceCollection services, bool isLifetimeLinked)
        where TApplication : Application
    {
        // The HostingContext will have a valid Application, a Dispatcher and its Scheduler, once the UIThread is started.
        var hostingContext = new HostingContext
        {
            IsLifetimeLinked = isLifetimeLinked,
            Application = null!,
            Dispatcher = null!,
            DispatcherScheduler = null!,
        };

        _ = services
            .AddSingleton(hostingContext)
            .AddSingleton<IUserInterfaceThread, UserInterfaceThread>()
            .AddHostedService<UserInterfaceHostedService>()
            .AddSingleton<TApplication>();

        if (typeof(TApplication) != typeof(Application))
        {
            _ = services.AddSingleton<Application>(sp => sp.GetRequiredService<TApplication>());
        }
    }
}
