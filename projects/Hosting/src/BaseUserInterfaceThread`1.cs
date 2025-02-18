// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DroidNet.Hosting;

/// <summary>
/// Represents a base class for a user interface thread in a hosted application.
/// </summary>
/// <typeparam name="T">
/// The concrete type of the class extending <see cref="BaseHostingContext" /> which will provide
/// the necessary options to set up the User Interface.
/// </typeparam>
public abstract partial class BaseUserInterfaceThread<T> : IDisposable, IUserInterfaceThread
    where T : BaseHostingContext
{
    private readonly IHostApplicationLifetime hostApplicationLifetime;
    private readonly ILogger logger;
    private readonly ManualResetEvent serviceManualResetEvent = new(initialState: false);

    /// <summary>
    /// This manual reset event is signaled when the UI thread completes. It is primarily used in
    /// testing environments to ensure that the thread execution completes before the test results
    /// are verified.
    /// </summary>
    private readonly ManualResetEvent uiThreadCompletion = new(initialState: false);

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="BaseUserInterfaceThread{T}" /> class.
    /// </summary>
    /// <remarks>
    /// This constructor creates a new thread that runs the UI. The thread is set to be a background
    /// thread with a single-threaded apartment state. The thread will wait for a signal from the
    /// <see cref="serviceManualResetEvent" /> before starting the user interface. The constructor
    /// also calls the <see cref="BeforeStart" /> and <see cref="OnCompletion" /> methods to perform
    /// any initialization and cleanup tasks.
    /// </remarks>
    /// <param name="lifetime">
    /// The hosted application lifetime. Used when the hosting context indicates that the UI and the
    /// hosted application lifetimes are linked.
    /// </param>
    /// <param name="context">
    /// The UI service hosting context, partially populated with the configuration options for the
    /// UI thread.
    /// </param>
    /// <param name="logger">The logger to be used by this class.</param>
    protected BaseUserInterfaceThread(IHostApplicationLifetime lifetime, T context, ILogger logger)
    {
        this.hostApplicationLifetime = lifetime;
        this.HostingContext = context;
        this.logger = logger;

        // Create a thread which runs the UI
        var newUiThread = new Thread(
            () =>
            {
                this.BeforeStart();
                _ = this.serviceManualResetEvent.WaitOne(); // wait for the signal to actually start
                this.HostingContext.IsRunning = true;
                this.DoStart();
                this.LogUserInterfaceThreadCompleted();
                this.OnCompletion();
            })
        {
            Name = "User Interface Thread",
            IsBackground = true,
        };

        // Set the apartment state
        newUiThread.SetApartmentState(ApartmentState.STA);

        // Transition the new UI thread to the RUNNING state. Note that the
        // thread will actually start after the `serviceManualResetEvent` is
        // set.
        newUiThread.Start();
    }

    /// <summary>
    /// Gets the hosting context for the user interface service.
    /// </summary>
    /// <value>
    /// Although never <see langword="null" />, the different fields of the hosting context may or
    /// may not contain valid values depending on the current state of the User Interface thread.
    /// Refer to the concrete class documentation.
    /// </value>
    protected T HostingContext { get; }

    /// <summary>
    /// Actually starts the User Interface thread by setting the underlying <see cref="ManualResetEvent" />.
    /// </summary>
    /// <remarks>
    /// Initially, the User Interface thread is created and transitioned into the `RUNNING` state,
    /// but it is waiting to be explicitly started via the manual reset event so that we can ensure
    /// everything required for the UI is initialized before we start it. The responsibility for
    /// triggering this rests with the User Interface hosted service.
    /// </remarks>
    public void StartUserInterface() => this.serviceManualResetEvent.Set();

    /// <inheritdoc />
    public abstract Task StopUserInterfaceAsync();

    /// <summary>
    /// Wait until the created User Interface Thread completes its execution.
    /// </summary>
    public void AwaitUiThreadCompletion() => this.uiThreadCompletion.WaitOne();

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="BaseUserInterfaceThread{T}" /> and
    /// optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true" /> to release both managed and unmanaged resources; <see langword="false" />
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            // Dispose of managed resources
            this.serviceManualResetEvent.Dispose();
            this.uiThreadCompletion.Dispose();
        }

        /* Dispose of unmanaged resources */

        this.isDisposed = true;
    }

    /// <summary>
    /// Called before the UI thread is started to do any initialization work.
    /// </summary>
    protected abstract void BeforeStart();

    /// <summary>
    /// Do the work needed to actually start the User Interface thread.
    /// </summary>
    protected abstract void DoStart();

    /// <summary>
    /// Immediately requests the User Interface thread to stop.
    /// </summary>
    protected abstract void StopUserInterface();

    /// <summary>
    /// Called upon completion of the UI thread (i.e. no more UI). Will eventually request the
    /// hosted application to stop depending on whether the UI lifecycle and the application
    /// lifecycle are linked or not.
    /// </summary>
    /// <seealso cref="BaseHostingContext.IsLifetimeLinked" />
    private void OnCompletion()
    {
        Debug.Assert(this.HostingContext.IsRunning, "Expecting the `IsRunning` flag to be set when `OnCompletion()` is called");

        this.StopUserInterface();
        _ = this.uiThreadCompletion.Set();

        if (this.HostingContext.IsLifetimeLinked)
        {
            this.LogStoppingHostApplication();

            if (!this.hostApplicationLifetime.ApplicationStopped.IsCancellationRequested &&
                !this.hostApplicationLifetime.ApplicationStopping.IsCancellationRequested)
            {
                this.hostApplicationLifetime.StopApplication();
            }
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "User interface thread completed.")]
    private partial void LogUserInterfaceThreadCompleted();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting application shutdown due to user interface completion and linked lifetimes...")]
    private partial void LogStoppingHostApplication();
}
