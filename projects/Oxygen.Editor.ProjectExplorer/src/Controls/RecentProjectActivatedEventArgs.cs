// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Provides data for the <see cref="RecentProjectsListViewModel.ItemActivated"/> event.
/// </summary>
internal class RecentProjectActivatedEventArgs(ProjectItemWithThumbnail item) : EventArgs
{
    /// <summary>
    /// Gets the project item that was activated.
    /// </summary>
    public ProjectItemWithThumbnail Item => item;
}
