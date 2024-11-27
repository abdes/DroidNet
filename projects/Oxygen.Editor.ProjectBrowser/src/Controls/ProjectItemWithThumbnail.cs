// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Represents a project item with a thumbnail image.
/// </summary>
internal sealed class ProjectItemWithThumbnail
{
    /// <summary>
    /// Gets the project information.
    /// </summary>
    public required IProjectInfo ProjectInfo { get; init; }

    /// <summary>
    /// Gets the thumbnail image source.
    /// </summary>
    public required ImageSource Thumbnail { get; init; }

    /// <summary>
    /// Compares project items by name.
    /// </summary>
    public sealed class ByNameComparer : IComparer
    {
        /// <inheritdoc/>
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return string.CompareOrdinal(item1.ProjectInfo.Name, item2.ProjectInfo.Name);
        }
    }

    /// <summary>
    /// Compares project items by last used date.
    /// </summary>
    public sealed class ByLastUsedOnComparer : IComparer
    {
        /// <inheritdoc/>
        public int Compare(object? x, object? y)
        {
            var item1 = (ProjectItemWithThumbnail)x!;
            var item2 = (ProjectItemWithThumbnail)y!;
            return DateTime.Compare(item1.ProjectInfo.LastUsedOn, item2.ProjectInfo.LastUsedOn);
        }
    }
}
