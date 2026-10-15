// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
/// Cooks the active project's changed source files once they settle, while the project's Cook on change setting is on.
/// </summary>
/// <remarks>
/// The service hears of changes from the asset catalog, the same way for a Save in the editor and a change made by
/// another tool. <see cref="INotifyPropertyChanged.PropertyChanged"/> is raised on the thread that changed the setting
/// or activated the project.
/// </remarks>
public interface IAutomaticCookService : INotifyPropertyChanged
{
    /// <summary>Gets a value indicating whether the active project cooks changed source files without being asked.</summary>
    public bool CookOnChange { get; }

    /// <summary>Saves the active project's Cook on change setting and applies it at once.</summary>
    /// <param name="enabled">Whether changed source files are cooked without being asked.</param>
    /// <param name="cancellationToken">Cancels the save.</param>
    /// <returns>Completion after the setting is saved and applied.</returns>
    public Task SetCookOnChangeAsync(bool enabled, CancellationToken cancellationToken = default);
}
