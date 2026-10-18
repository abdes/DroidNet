// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT


namespace Oxygen.Editor.Projects;

/// <summary>
///     Owns the currently active project context.
/// </summary>
public interface IProjectContextService
{
    /// <summary>
    ///     Gets the active project context, or <see langword="null"/> when no project is active.
    /// </summary>
    public ProjectContext? ActiveProject { get; }

    /// <summary>
    ///     Gets an observable stream that replays the current context to new subscribers.
    /// </summary>
    public IObservable<ProjectContext?> ProjectChanged { get; }

    /// <summary>
    ///     Gets the active project's current settings, or the defaults when no project is active.
    /// </summary>
    public ProjectSettings Settings { get; }

    /// <summary>
    ///     Gets an observable stream of the settings that <see cref="UpdateSettings"/> gives the active project. Activating
    ///     a project does not raise it; <see cref="ProjectChanged"/> does.
    /// </summary>
    public IObservable<ProjectSettings> SettingsChanged { get; }

    /// <summary>
    ///     Replaces the active project context.
    /// </summary>
    /// <param name="context">The new active context.</param>
    public void Activate(ProjectContext context);

    /// <summary>
    ///     Clears the active project context.
    /// </summary>
    public void Close();

    /// <summary>
    ///     Gives the active project new settings without activating it again, so its services keep their work.
    /// </summary>
    /// <param name="settings">The settings, already saved in the project's manifest.</param>
    public void UpdateSettings(ProjectSettings settings);
}
