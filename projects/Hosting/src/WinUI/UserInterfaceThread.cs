// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.WinUI;

using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using WinRT;

/// <summary>
/// Implementation for a WinUI based UI thread. This is basically a drop-in
/// replacement for the bootstrap code auto-generated by the WinUI XAML in the
/// `Main` entry point.
/// </summary>
/// <param name="serviceProvider">
/// The Dependency Injector's <see cref="IServiceProvider" />.
/// </param>
/// <param name="lifetime">
/// The host application lifetime. Should be provided by the DI injector and is
/// used when the hosting context indicates that the UI and the host
/// application lifetimes are linked.
/// </param>
/// <param name="context">
/// The UI service hosting context. Should be provided by the DI injector and
/// partially populated with the configuration options for the UI thread.
/// </param>
/// <param name="loggerFactory">
/// Used to obtain a logger for this class. If not possible, a
/// <see cref="NullLogger" /> will be used instead.
/// </param>
public class UserInterfaceThread(
    IServiceProvider serviceProvider,
    IHostApplicationLifetime lifetime,
    HostingContext context,
    ILoggerFactory? loggerFactory) : BaseUserInterfaceThread<HostingContext>(
    lifetime,
    context,
    loggerFactory?.CreateLogger<UserInterfaceThread>() ?? MakeNullLogger())
{
    /// <inheritdoc />
    public override Task StopUserInterfaceAsync()
    {
        Debug.Assert(
            this.HostingContext.Application is not null,
            "Expecting the `Application` in the context to not be null.");

        TaskCompletionSource completion = new();
        _ = this.HostingContext.Dispatcher!.TryEnqueue(
            () =>
            {
                this.HostingContext.Application?.Exit();
                completion.SetResult();
            });
        return completion.Task;
    }

    /// <inheritdoc />
    protected override void BeforeStart() => ComWrappersSupport.InitializeComWrappers();

    /// <inheritdoc />
    protected override void DoStart() => Application.Start(
        _ =>
        {
            this.HostingContext.Dispatcher = DispatcherQueue.GetForCurrentThread();
            DispatcherQueueSynchronizationContext context = new(this.HostingContext.Dispatcher);
            SynchronizationContext.SetSynchronizationContext(context);

            this.HostingContext.Application = serviceProvider.GetRequiredService<Application>();

            /*
             * TODO: here we can add code that initializes the UI before the
             * main window is created and activated For example: unhandled
             * exception handlers, maybe instancing, activation, etc...
             */

            // NOTE: First window creation is to be handled in Application.OnLaunched()
        });

    private static ILogger MakeNullLogger() => NullLoggerFactory.Instance.CreateLogger<UserInterfaceThread>();
}
