// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>Ends a window's use of the preview settings with the window: closing it deactivates them for its project.</summary>
/// <param name="settings">The preview settings the window's scene editor restored.</param>
/// <param name="projects">The active project.</param>
internal sealed class PreviewSettingsLifetime(PreviewSettingsService settings, IProjectContextService projects) : IDisposable
{
    /// <inheritdoc/>
    public void Dispose()
    {
        if (projects.ActiveProject is { } project)
        {
            settings.Deactivate(project);
        }
    }
}
