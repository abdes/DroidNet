// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Desktop;

namespace Oxygen.Editor.Services;

/// <summary>The folder of the active project, which desktop tools open as their workspace.</summary>
/// <param name="projects">The active project.</param>
internal sealed class ActiveProjectFolder(IProjectContextService projects) : IProjectFolder
{
    /// <inheritdoc/>
    public string? Path => projects.ActiveProject?.ProjectRoot.FullPath;
}
