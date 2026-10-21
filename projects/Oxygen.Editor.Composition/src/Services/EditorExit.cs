// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Services;

/// <summary>
///     The editor's part of closing its last window, which exits the editor: the project session closes, then the
///     engine shuts down, before the window is destroyed.
/// </summary>
/// <remarks>
///     Closing a session's last workspace window does not exit: it returns to the Start window. The editor exits when
///     the window that closes is the last one and no session holds it, such as the Start window.
/// </remarks>
internal sealed partial class EditorExit : IWindowCloseParticipant
{
    private readonly ProjectSessionHost sessions;
    private readonly IWindowManagerService windows;
    private readonly EngineShutdownService engine;
    private readonly HostingContext hosting;
    private readonly ILogger logger;

    /// <summary>Initializes a new instance of the <see cref="EditorExit" /> class.</summary>
    /// <param name="sessions">The project session's host.</param>
    /// <param name="windows">The window manager, which knows the session's windows.</param>
    /// <param name="engine">Shuts the engine down.</param>
    /// <param name="hosting">The UI thread, where the session closes.</param>
    /// <param name="loggerFactory">Optional factory of the participant's logger.</param>
    public EditorExit(ProjectSessionHost sessions, IWindowManagerService windows, EngineShutdownService engine, HostingContext hosting, ILoggerFactory? loggerFactory = null)
    {
        this.sessions = sessions;
        this.windows = windows;
        this.engine = engine;
        this.hosting = hosting;
        this.logger = loggerFactory?.CreateLogger<EditorExit>() ?? NullLogger<EditorExit>.Instance;
    }

    /// <inheritdoc />
    public WindowCloseLevel Level => WindowCloseLevel.Application;

    /// <inheritdoc />
    public Task<WindowCloseStep?> PrepareAsync(WindowClose close)
    {
        ArgumentNullException.ThrowIfNull(close);
        var window = close.Window;
        var isSessionWindow = this.IsSessionWindow(window);
        var exits = close.IsLastWindow && !isSessionWindow;
        this.LogDecided(window, close.IsLastWindow, isSessionWindow, exits);
        return Task.FromResult<WindowCloseStep?>(
            exits ? new WindowCloseStep(finalize: () => this.ExitAsync(window)) : WindowCloseStep.None);
    }

    private bool IsSessionWindow(WindowId window)
        => this.sessions.Current is { } session && EditorWindows.WindowsOf(this.windows, session).Any(managed => managed.Id == window);

    private async Task ExitAsync(WindowId window)
    {
        this.LogExiting(window);
        await this.hosting.Dispatcher.DispatchAsync(this.sessions.CloseAsync).ConfigureAwait(false);
        await this.engine.ShutdownAsync().ConfigureAwait(false);
    }
}
