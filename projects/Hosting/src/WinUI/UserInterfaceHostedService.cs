// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Hosting.WinUI;

/// <summary>
/// A long-running service that will execute the User Interface thread.
/// </summary>
/// <remarks>
/// <para>
/// Should be registered (only once) in the services collection with the
/// <see cref="ServiceCollectionHostedServiceExtensions.AddHostedService{THostedService}(IServiceCollection)">
/// AddHostedService </see> extension method.
/// </para>
/// <para>
/// Expects the <see cref="UserInterfaceThread" /> and <see cref="HostingContext" /> singleton
/// instances to be setup in the dependency injector.
/// </para>
/// <para>
/// The <see cref="HostingContext" /> is used to manage the lifecycle and configuration of the UI thread.
/// It provides the necessary context, such as the dispatcher queue and the application instance, to the
/// <see cref="UserInterfaceThread" />. The <see cref="BaseHostingContext.IsLifetimeLinked" /> property determines
/// whether the UI thread's lifecycle is linked to the application's lifecycle. If linked, terminating the UI thread
/// will also terminate the application and vice versa.
/// </para>
/// </remarks>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a <see cref="NullLogger" />
/// if we fail to obtain a <see cref="ILogger" /> from the Dependency Injector.
/// </param>
/// <param name="uiThread">
/// The <see cref="IUserInterfaceThread" /> instance.
/// </param>
/// <param name="context">
/// The <see cref="HostingContext" /> instance.
/// </param>
public partial class UserInterfaceHostedService(
    HostingContext context,
    IUserInterfaceThread uiThread,
    ILoggerFactory? loggerFactory = null) : IHostedService
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1823:Avoid unused private fields", Justification = "used by generated logging methods")]
    private readonly ILogger logger = loggerFactory?.CreateLogger<UserInterfaceHostedService>() ?? NullLoggerFactory.Instance.CreateLogger<UserInterfaceHostedService>();

    /// <inheritdoc />
    public Task StartAsync(CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            return Task.CompletedTask;
        }

        // Make the UI thread go
        uiThread.StartUserInterface();
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public Task StopAsync(CancellationToken cancellationToken)
    {
        if (cancellationToken.IsCancellationRequested || !context.IsRunning)
        {
            return Task.CompletedTask;
        }

        this.LogStoppingUserInterfaceThread();
        return uiThread.StopUserInterfaceAsync();
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Stopping user interface thread due to application exiting.")]
    partial void LogStoppingUserInterfaceThread();
}
