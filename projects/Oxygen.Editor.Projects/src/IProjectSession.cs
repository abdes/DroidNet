// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     An open project: what the editor works on from the project's activation until the project closes. Every service
///     of the session is created for this project and disposed with it.
/// </summary>
/// <remarks>
///     The project stays the same for the whole session: the same <see cref="ProjectContext.ProjectId" /> and root. Only
///     a change of its content mounts gives it a new <see cref="Context" />, which <see cref="ContextChanged" /> reports.
///     A service reads <see cref="Context" /> when it acts, so it never keeps an earlier one.
/// </remarks>
public interface IProjectSession
{
    /// <summary>
    ///     Occurs once a change of the project's content mounts has replaced <see cref="Context" />, on the thread that made
    ///     the change. Only the services whose state comes from the mounts follow it.
    /// </summary>
    public event EventHandler? ContextChanged;

    /// <summary>
    ///     Occurs once <see cref="UpdateSettings" /> has replaced <see cref="Settings" />, on the thread that made the
    ///     change.
    /// </summary>
    public event EventHandler? SettingsChanged;

    /// <summary>Gets the session's project, as its content mounts are now.</summary>
    public ProjectContext Context { get; }

    /// <summary>
    ///     Gets the project's settings: those it was opened with, until <see cref="UpdateSettings" /> replaces them. A
    ///     change of the content mounts keeps them.
    /// </summary>
    public ProjectSettings Settings { get; }

    /// <summary>Gets the loaded project the session was opened for.</summary>
    public IProject Project { get; }

    /// <summary>
    ///     Gets the token that is cancelled when the session starts to close, before any of its services is disposed. Work
    ///     that may outlive the session's project runs under it.
    /// </summary>
    public CancellationToken Lifetime { get; }

    /// <summary>Replaces the project's settings, once they are saved, then reports it.</summary>
    /// <param name="settings">The project's new settings.</param>
    public void UpdateSettings(ProjectSettings settings);
}
