// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Provides data for the <see cref="RecentProjectsList.ItemActivated"/> event.
/// </summary>
public class ProjectItemActivatedEventArgs(IProjectInfo projectInfo) : EventArgs
{
    /// <summary>
    /// Gets the project information associated with the activated item.
    /// </summary>
    public IProjectInfo ProjectInfo => projectInfo;
}
