// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Sessions;

/// <summary>
///     The project session's part of closing its last workspace window: the session's other windows close their documents
///     with it, the Start window opens before the workspace window closes, so the editor always has a window, then the
///     session closes.
/// </summary>
/// <remarks>
///     <para>
///     Closing the session's last workspace window returns to the Start window instead of exiting the editor (shell
///     design section 6). The closing window's own documents take part in its close as every window's do; the documents
///     of the session's other windows are prepared with them, then committed with them. When one of them is kept, or the
///     Start window does not open, every window of the session stays open. A Start window that does not open once the
///     documents closed is published as a failed operation, so the user knows why the workspace stayed open without
///     them.
///     </para>
///     <para>
///     Aura's window manager knows the session's windows: each carries the session in its metadata, and a workspace
///     window has the workspace category.
///     </para>
/// </remarks>
internal sealed partial class SessionWindowClose : IWindowCloseParticipant
{
    private const string CloseOperation = "Workspace.Close";
    private const string StartWindowFailedCode = DiagnosticCodes.WorkspacePrefix + "START_WINDOW_FAILED";

    private readonly ProjectSessionHost sessions;
    private readonly IWindowManagerService windows;
    private readonly IRouter router;
    private readonly HostingContext hosting;
    private readonly IOperationResultPublisher results;
    private readonly IStatusReducer status;
    private readonly ILogger logger;

    /// <summary>Initializes a new instance of the <see cref="SessionWindowClose" /> class.</summary>
    /// <param name="sessions">The project session's host.</param>
    /// <param name="windows">The window manager, which knows the session's windows.</param>
    /// <param name="router">The application's router, which opens the Start window.</param>
    /// <param name="hosting">The UI thread, where documents close, windows open and the session closes.</param>
    /// <param name="results">Publishes a close that left the workspace window open.</param>
    /// <param name="status">Reduces the published failure.</param>
    /// <param name="loggerFactory">Optional factory of the participant's logger.</param>
    public SessionWindowClose(
        ProjectSessionHost sessions,
        IWindowManagerService windows,
        IRouter router,
        HostingContext hosting,
        IOperationResultPublisher results,
        IStatusReducer status,
        ILoggerFactory? loggerFactory = null)
    {
        this.sessions = sessions;
        this.windows = windows;
        this.router = router;
        this.hosting = hosting;
        this.results = results;
        this.status = status;
        this.logger = loggerFactory?.CreateLogger<SessionWindowClose>() ?? NullLogger<SessionWindowClose>.Instance;
    }

    /// <inheritdoc />
    public WindowCloseLevel Level => WindowCloseLevel.Session;

    /// <inheritdoc />
    public async Task<WindowCloseStep?> PrepareAsync(WindowClose close)
    {
        ArgumentNullException.ThrowIfNull(close);
        var window = close.Window;
        // A session that closes its windows itself has closed their documents already.
        if (this.sessions.Current is not { } session || this.sessions.IsClosing
            || !EditorWindows.IsLastWorkspaceWindow(this.windows, session, window))
        {
            this.LogNotLastWindow(window, this.sessions.Current is not null, this.sessions.IsClosing);
            return WindowCloseStep.None;
        }

        // The closing window's own documents take part in its close as every window's do.
        var prepared = await this.hosting.Dispatcher.DispatchAsync(() => this.sessions.PrepareCloseDocumentsAsync(except: window)).ConfigureAwait(false);
        return prepared is null
            ? null
            : new WindowCloseStep(
                commit: () => this.hosting.Dispatcher.DispatchAsync(() => this.CommitAsync(prepared, window)),
                finalize: () => this.hosting.Dispatcher.DispatchAsync(() => this.CloseSessionAsync(window)),
                prepared: prepared);
    }

    private async Task<bool> CommitAsync(DocumentCloseTransaction prepared, WindowId window)
        => await prepared.CommitAsync().ConfigureAwait(true) && await this.OpenStartWindowAsync(window).ConfigureAwait(true);

    private async Task<bool> OpenStartWindowAsync(WindowId window)
    {
        this.LogReturningToStart(window);
        var opened = await EditorWindows.NavigateAsync(
                this.router,
                EditorWindows.StartPage,
                new FullNavigation { Target = new Target { Name = EditorWindows.StartTarget } })
            .ConfigureAwait(true);
        if (!opened)
        {
            this.LogStartWindowFailed(window);
            this.PublishStartWindowFailed();
            return false;
        }

        var start = this.windows.OpenWindows.Where(static managed => managed.Category == EditorWindows.StartCategory).Select(static managed => managed.Id.Value).ToList();
        this.LogStartWindowOpened(window, string.Join(", ", start));
        return true;
    }

    private async Task CloseSessionAsync(WindowId window)
    {
        this.LogClosingSession(window);
        await this.sessions.CloseAsync().ConfigureAwait(true);
        this.LogSessionClosed(window, this.windows.OpenWindows.Count);
    }

    private void PublishStartWindowFailed()
    {
        var diagnostics = new List<DiagnosticRecord>
        {
            new()
            {
                OperationId = Guid.NewGuid(),
                Domain = FailureDomain.WorkspaceActivation,
                Severity = DiagnosticSeverity.Error,
                Code = StartWindowFailedCode,
                Message = "The Start window did not open.",
            },
        }.AsReadOnly();
        this.results.Publish(new OperationResult
        {
            OperationId = diagnostics[0].OperationId,
            OperationKind = CloseOperation,
            Status = this.status.Reduce(primaryGoalCompleted: false, wasCancelled: false, diagnostics),
            Severity = this.status.ComputeSeverity(diagnostics),
            Title = "The workspace window stayed open",
            Message = "Its documents closed, but the Start window did not open, so the workspace window and its project stay open.",
            CompletedAt = DateTimeOffset.Now,
            Diagnostics = diagnostics,
        });
    }
}
