// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Desktop;

namespace Oxygen.Editor.Sessions;

/// <summary>The folder of the session's project, which desktop tools open as their workspace.</summary>
/// <param name="session">The project session.</param>
internal sealed class ProjectFolder(IProjectSession session) : IProjectFolder
{
    /// <inheritdoc/>
    public string Path => session.Context.ProjectRoot.FullPath;
}
