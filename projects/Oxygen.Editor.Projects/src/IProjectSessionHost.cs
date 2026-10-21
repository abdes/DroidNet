// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Opens and closes the editor's project session. At most one session is open at a time.
/// </summary>
/// <remarks>
///     The host is the only place that knows which project is open. Its consumers are the editor's own: the activation of
///     projects, the workspace windows, which open in the session, the change of a project's content mounts, and closing
///     and exiting the editor. A service of the session receives its session instead.
/// </remarks>
public interface IProjectSessionHost
{
    /// <summary>Occurs after a session has opened or closed.</summary>
    public event EventHandler? SessionChanged;

    /// <summary>Gets the open session, or <see langword="null" /> when no project is open.</summary>
    public IProjectSession? Current { get; }

    /// <summary>Opens the session of a project.</summary>
    /// <param name="context">The project to open.</param>
    /// <param name="project">The loaded project, accepted by the project manager.</param>
    /// <returns>The open session.</returns>
    /// <exception cref="InvalidOperationException">A session is already open.</exception>
    public Task<IProjectSession> OpenAsync(ProjectContext context, IProject project);

    /// <summary>
    ///     Gives the open session's project its new content mounts: <paramref name="next" /> becomes the session's
    ///     context, then the session raises <see cref="IProjectSession.ContextChanged" />.
    /// </summary>
    /// <param name="next">The same project, with the mounts its saved configuration now has.</param>
    /// <exception cref="InvalidOperationException">
    ///     No session is open, or <paramref name="next" /> is another project: another id or another root. Another project
    ///     always opens a new session.
    /// </exception>
    public void ChangeContext(ProjectContext next);

    /// <summary>
    ///     Closes the open session: cancels its lifetime, then disposes the scopes of its windows and its own services.
    ///     Nothing happens when no session is open.
    /// </summary>
    /// <returns>A task that completes when the session is closed.</returns>
    public Task CloseAsync();
}
