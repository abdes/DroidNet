// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Provides data for the <see cref="RecentProjectsList.ItemActivated"/> event.
/// </summary>
public class ProjectItemActivatedEventArgs(RecentProjectEntry entry) : EventArgs
{
    /// <summary>
    /// Gets the recent project entry associated with the activated item.
    /// </summary>
    public RecentProjectEntry Entry => entry;
}
