// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Shuts the engine down on the UI thread when the editor exits, before the host stops its UI.</summary>
/// <remarks>
///     The editor's exit shuts the engine down before its last window is destroyed; the host's stop does it again, which
///     does nothing once the engine is gone.
/// </remarks>
public sealed partial class EngineShutdownService : IHostedService
{
    private readonly IEngineService engine;
    private readonly IOperationResultPublisher results;
    private readonly Func<Func<Task>, Task> dispatch;
    private readonly ILogger logger;

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

    /// <inheritdoc/>
    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    /// <inheritdoc/>
    /// <remarks>Registered after the UI hosted service, so the host stops it before its dispatcher.</remarks>
    public Task StopAsync(CancellationToken cancellationToken) => this.ShutdownAsync();

    /// <summary>Shuts the engine down on the UI thread; a failure is published, not thrown.</summary>
    /// <returns>A task that completes when the engine is shut down, or immediately when there is no engine.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The application shutdown boundary records runtime failure without interrupting other service cleanup.")]
    public async Task ShutdownAsync()
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
}
