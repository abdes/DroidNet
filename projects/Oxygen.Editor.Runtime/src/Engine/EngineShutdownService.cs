// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Observes engine shutdown before final window and host UI teardown.</summary>
public sealed partial class EngineShutdownService : IHostedService
{
    private readonly IEngineService engine;
    private readonly Lock windowTrackingLock = new();
    private readonly IOperationResultPublisher results;
    private readonly Func<Func<Task>, Task> dispatch;
    private readonly ILogger logger;
    private readonly ConcurrentDictionary<WindowId, byte> closingWindows = new();
    private IWindowManagerService? windows;
    private bool stopping;

    /// <summary>Initializes a new instance of the <see cref="EngineShutdownService"/> class.</summary>
    /// <param name="engine">The shared runtime service.</param>
    /// <param name="results">The existing diagnostics publisher.</param>
    /// <param name="hosting">The UI dispatcher context.</param>
    /// <param name="loggerFactory">The optional logging factory.</param>
    public EngineShutdownService(IEngineService engine, IOperationResultPublisher results, HostingContext hosting, ILoggerFactory? loggerFactory = null)
        : this(engine, results, action => hosting.Dispatcher.DispatchAsync(action), loggerFactory)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="EngineShutdownService"/> class with explicit dispatch.</summary>
    /// <param name="engine">The runtime service.</param>
    /// <param name="results">The diagnostic publisher.</param>
    /// <param name="dispatch">Runs cleanup on the UI thread.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    internal EngineShutdownService(IEngineService engine, IOperationResultPublisher results, Func<Func<Task>, Task> dispatch, ILoggerFactory? loggerFactory = null)
    {
        this.engine = engine;
        this.results = results;
        this.dispatch = dispatch;
        this.logger = loggerFactory?.CreateLogger<EngineShutdownService>() ?? NullLogger<EngineShutdownService>.Instance;
    }

    /// <summary>Connects window lifecycle tracking after the application creates its UI dispatcher.</summary>
    /// <param name="windows">The initialized application window manager.</param>
    public void ObserveWindows(IWindowManagerService windows)
    {
        ArgumentNullException.ThrowIfNull(windows);
        lock (this.windowTrackingLock)
        {
            if (this.stopping || ReferenceEquals(this.windows, windows))
            {
                return;
            }

            if (this.windows is not null)
            {
                throw new InvalidOperationException("The runtime shutdown service already observes another window manager.");
            }

            this.windows = windows;
            windows.WindowClosing += this.OnWindowClosingAsync;
            windows.WindowClosed += this.OnWindowClosedAsync;
        }
    }

    /// <inheritdoc/>
    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <inheritdoc/>
    public async Task StopAsync(CancellationToken cancellationToken)
    {
        lock (this.windowTrackingLock)
        {
            this.stopping = true;
        }

        // Registered after the UI hosted service, so the host stops us before its dispatcher.
        await this.ShutdownAsync().ConfigureAwait(false);
        lock (this.windowTrackingLock)
        {
            if (this.windows is not null)
            {
                this.windows.WindowClosing -= this.OnWindowClosingAsync;
                this.windows.WindowClosed -= this.OnWindowClosedAsync;
            }
        }
    }

    private Task OnWindowClosingAsync(object? sender, WindowClosingEventArgs args)
    {
        args.AddFinalizationTask(() =>
        {
            _ = this.closingWindows.TryAdd(args.WindowId, 0);
            return this.windows!.OpenWindows.All(window => this.closingWindows.ContainsKey(window.Id))
                ? this.ShutdownAsync()
                : Task.CompletedTask;
        });
        return Task.CompletedTask;
    }

    private Task OnWindowClosedAsync(object? sender, WindowClosedEventArgs args)
    {
        _ = this.closingWindows.TryRemove(args.WindowId, out _);
        return Task.CompletedTask;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The application shutdown boundary records runtime failure without interrupting other service cleanup.")]
    private async Task ShutdownAsync()
    {
        if (this.engine.State is EngineServiceState.NoEngine)
        {
            return;
        }

        try
        {
            await this.dispatch(() => this.engine.ShutdownAsync().AsTask()).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            this.LogShutdownFailed(exception, this.engine.State);
            this.results.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = "Runtime.Shutdown",
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Error,
                Title = "Engine shutdown encountered an error",
                Message = exception.Message,
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }

    [LoggerMessage(Level = LogLevel.Error, Message = "Engine shutdown failed. Remaining runtime state: {State}.")]
    private partial void LogShutdownFailed(Exception exception, EngineServiceState state);
}
