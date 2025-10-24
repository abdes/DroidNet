// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.ProjectBrowser;

/// <summary>
/// Represents the settings for the Project Browser module in the Oxygen Editor.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="ProjectBrowserSettings"/> class extends the <see cref="WindowedModuleSettings"/> class to
/// include properties specific to the Project Browser module. These properties are automatically persisted
/// and retrieved using the <see cref="IEditorSettingsManager"/> when the <see cref="ModuleSettings.SaveAsync"/> and
/// <see cref="ModuleSettings.LoadAsync"/> methods are called.
/// </para>
/// <para>
/// The <see cref="LastSaveLocation"/> property represents the last location where a project was saved.
/// </para>
/// </remarks>
/// <param name="settingsManager">The settings manager responsible for persisting settings.</param>
internal sealed partial class ProjectBrowserSettings(IEditorSettingsManager settingsManager)
    : WindowedModuleSettings(settingsManager, typeof(ProjectBrowserSettings).Namespace!)
{
    private string lastSaveLocation = string.Empty;

    /// <summary>
    /// Gets or sets the last location where a project was saved.
    /// </summary>
    /// <value>
    /// A <see cref="string"/> representing the last save location.
    /// </value>
    [Persisted]
    public string LastSaveLocation
    {
        get => this.lastSaveLocation;
        set => this.SetProperty(ref this.lastSaveLocation, value);
    }
}
