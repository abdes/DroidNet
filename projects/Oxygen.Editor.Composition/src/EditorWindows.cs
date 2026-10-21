// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.UI;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor;

/// <summary>
///     The editor's top-level windows: the targets the application's router opens them in, the page each one shows
///     first, and how Aura's window manager tells their kind and their session.
/// </summary>
/// <remarks>
///     Aura's window manager is the editor's window registry: a window's kind is its <see cref="WindowCategory" />, and a
///     window of a project session carries the session in its metadata.
/// </remarks>
public static class EditorWindows
{
    /// <summary>The target of the Start window, which shows the Project Explorer.</summary>
    public const string StartTarget = "wnd-pe";

    /// <summary>The target of a project's workspace window.</summary>
    public const string WorkspaceTarget = "wnd-we";

    /// <summary>The page the Start window shows first.</summary>
    public const string StartPage = "/pe/home";

    /// <summary>The page of a project's workspace window.</summary>
    public const string WorkspacePage = "/we";

    /// <summary>The metadata key under which a window of a project session holds its <see cref="IProjectSession" />.</summary>
    public const string SessionMetadataKey = "Oxygen.ProjectSession";

    /// <summary>Gets the category of the Start window.</summary>
    public static WindowCategory StartCategory { get; } = new(StartTarget);

    /// <summary>Gets the category of a project's workspace window.</summary>
    public static WindowCategory WorkspaceCategory { get; } = new(WorkspaceTarget);

    /// <summary>Gets the open windows of a project session, as Aura's window manager knows them.</summary>
    /// <param name="windows">The window manager.</param>
    /// <param name="session">The session.</param>
    /// <returns>The session's windows.</returns>
    public static IReadOnlyList<IManagedWindow> WindowsOf(IWindowManagerService windows, IProjectSession session)
    {
        ArgumentNullException.ThrowIfNull(windows);
        return [.. windows.OpenWindows.Where(window => window.Metadata is { } metadata
            && metadata.TryGetValue(SessionMetadataKey, out var owner) && ReferenceEquals(owner, session))];
    }

    /// <summary>Tells whether a window is the last workspace window of a project session.</summary>
    /// <param name="windows">The window manager.</param>
    /// <param name="session">The session.</param>
    /// <param name="window">The window that closes.</param>
    /// <returns><see langword="true" /> when no other workspace window of the session is open.</returns>
    public static bool IsLastWorkspaceWindow(IWindowManagerService windows, IProjectSession session, WindowId window)
    {
        var workspaces = WindowsOf(windows, session).Where(static managed => managed.Category == WorkspaceCategory).ToList();
        return workspaces.Count == 1 && workspaces[0].Id == window;
    }

    /// <summary>Navigates the application's router and reports whether the navigation succeeded.</summary>
    /// <param name="router">The application's router.</param>
    /// <param name="url">The page to navigate to.</param>
    /// <param name="navigation">The navigation's options, which identify its events.</param>
    /// <returns><see langword="false" /> when the router reported a failure of this navigation.</returns>
    /// <remarks>The router reports a failed navigation as an event, not as an exception: this watches its own.</remarks>
    public static async Task<bool> NavigateAsync(IRouter router, string url, FullNavigation navigation)
    {
        ArgumentNullException.ThrowIfNull(router);
        var failed = false;
        using var failures = router.Events.OfType<NavigationError>()
            .Where(error => ReferenceEquals(error.Options, navigation))
            .Subscribe(_ => failed = true);
        await router.NavigateAsync(url, navigation).ConfigureAwait(true);
        return !failed;
    }
}
