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
    ///     Replaces the active project context.
    /// </summary>
    /// <param name="context">The new active context.</param>
    public void Activate(ProjectContext context);

    /// <summary>
    ///     Clears the active project context.
    /// </summary>
    public void Close();
}
