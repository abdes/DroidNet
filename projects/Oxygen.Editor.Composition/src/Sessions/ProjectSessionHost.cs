// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Sessions;

/// <summary>
///     Opens and closes the editor's project session, a scope under the editor scope that the session's windows are
///     created under.
/// </summary>
/// <remarks>
///     Opening creates the session's scope from <see cref="ProjectSessionModule.Compose" /> and registers the loaded
///     project and the session in it; a service reads the session's project from the session, so it never keeps one that
///     a mount change replaced. Closing cancels the session's lifetime first, so its work in flight stops, then disposes
///     its scope, which disposes the scopes of its windows before its own services. Aura's window manager knows the
///     session's windows: each one carries the session in its metadata (<see cref="EditorWindows.SessionMetadataKey" />).
/// </remarks>
public sealed partial class ProjectSessionHost : IProjectSessionHost, ISessionWindows
{
    private readonly ScopeFactory scopes;
    private readonly ScopeHandle editor;
    private readonly IWindowManagerService windows;
    private readonly ILogger logger;
    private ProjectSession? current;

    /// <summary>Initializes a new instance of the <see cref="ProjectSessionHost" /> class.</summary>
    /// <param name="scopes">The factory of the session's scope.</param>
    /// <param name="editor">The editor scope, which the session's scope is created under.</param>
    /// <param name="windows">The window manager, which closes the session's windows before the session ends.</param>
    /// <param name="loggerFactory">Optional factory of the host's logger.</param>
    public ProjectSessionHost(ScopeFactory scopes, ScopeHandle editor, IWindowManagerService windows, ILoggerFactory? loggerFactory = null)
    {
        this.scopes = scopes;
        this.editor = editor;
        this.windows = windows;
        this.logger = loggerFactory?.CreateLogger<ProjectSessionHost>() ?? NullLogger<ProjectSessionHost>.Instance;
    }

    /// <inheritdoc />
    public event EventHandler? SessionChanged;

    /// <inheritdoc />
    public IProjectSession? Current => this.current;

    /// <summary>
    ///     Gets a value indicating whether the open session is closing its windows. Their documents were closed by whoever
    ///     closes the session, so closing them asks nothing again.
    /// </summary>
    public bool IsClosing => this.current?.IsClosing == true;

    /// <inheritdoc />
    public Task<IProjectSession> OpenAsync(ProjectContext context, IProject project)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(project);
        if (this.current is not null)
        {
            throw new InvalidOperationException("A project session is already open; close it before opening another.");
        }

        var session = new ProjectSession(context, project);
        try
        {
            session.Scope = this.scopes.Create(
                this.editor,
                ProjectSessionModule.Compose(),
                registrations =>
                {
                    registrations.RegisterInstance(project);
                    registrations.RegisterInstance<IProjectSession>(session);
                });
        }
        catch (Exception failure)
        {
            session.Dispose();
            this.LogOpenFailed(context, failure);
            throw;
        }

        this.current = session;
        this.LogOpened(context);
        this.SessionChanged?.Invoke(this, EventArgs.Empty);
        return Task.FromResult<IProjectSession>(session);
    }

    /// <inheritdoc />
    public ScopeHandle OpenWindow(WindowId window, IEnumerable<IScopeModule<IWindowLayer>> modules)
    {
        var session = this.current ?? throw new InvalidOperationException("A window of a project session opens only in an open session.");
        var managed = this.windows.GetWindow(window)
            ?? throw new InvalidOperationException($"Window {window.Value} is not open: a window joins a session once Aura's window manager knows it.");
        var scope = this.scopes.Create(session.Scope!, modules, registrations => registrations.RegisterInstance(window));
        this.windows.SetMetadata(window, EditorWindows.SessionMetadataKey, session);
        managed.DocumentTabSource = scope.Resolver.Resolve<IDocumentTabs>();
        return scope;
    }

    /// <inheritdoc />
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId? except = null)
        => this.current?.Scope is { } scope
            ? scope.Resolver.Resolve<IDocumentTabs>().PrepareCloseAllWindowsAsync(except)
            : Task.FromResult<DocumentCloseTransaction?>(DocumentCloseTransaction.CreateEmpty());

    /// <inheritdoc />
    public void ChangeContext(ProjectContext next)
    {
        ArgumentNullException.ThrowIfNull(next);
        var session = this.current ?? throw new InvalidOperationException("Only an open project session has content mounts to change.");
        var previous = session.Context;
        if (next.ProjectId != previous.ProjectId || next.ProjectRoot != previous.ProjectRoot)
        {
            throw new InvalidOperationException("A content mount change stays within the session's project; another project opens a new session.");
        }

        this.LogContextChanged(next);
        session.ChangeContext(next);
    }

    /// <inheritdoc />
    /// <remarks>
    ///     The session's open windows close first, through Aura's window manager, then the session's lifetime ends and its
    ///     scope is disposed. The caller has closed the session's documents: closing its windows asks nothing again. A
    ///     window that is already closing finishes its own close.
    /// </remarks>
    public async Task CloseAsync()
    {
        if (this.current is not { IsClosing: false } session)
        {
            return;
        }

        session.IsClosing = true;
        this.LogClosing(session.Context);
        foreach (var window in EditorWindows.WindowsOf(this.windows, session))
        {
            if (!await this.windows.CloseWindowAsync(window.Id).ConfigureAwait(true))
            {
                this.LogWindowNotClosed(window.Id);
            }
        }

        // A window still open leaves the session before its tabs and its services go.
        foreach (var window in EditorWindows.WindowsOf(this.windows, session))
        {
            window.DocumentTabSource = null;
            this.windows.RemoveMetadata(window.Id, EditorWindows.SessionMetadataKey);
        }

        this.current = null;
        session.End();
        try
        {
            if (session.Scope is { } scope)
            {
                await scope.DisposeAsync().ConfigureAwait(true);
            }
        }
        catch (Exception failure)
        {
            this.LogDisposeFailed(session.Context, failure);
            throw;
        }
        finally
        {
            session.Dispose();
            this.LogClosed(session.Context);
            this.SessionChanged?.Invoke(this, EventArgs.Empty);
        }
    }
}
