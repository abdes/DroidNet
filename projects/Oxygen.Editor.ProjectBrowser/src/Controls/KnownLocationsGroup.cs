// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Oxygen.Editor.ProjectBrowser.Projects;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Represents a group of known locations within the project browser.
/// </summary>
/// <param name="items">The collection of <see cref="KnownLocation"/> items in this group.</param>
internal sealed partial class KnownLocationsGroup(IEnumerable<KnownLocation> items) : List<KnownLocation>(items)
{
    /// <summary>
    /// Gets or sets the order of the group.
    /// </summary>
    public required int Order { get; set; }

    /// <summary>
    /// Gets or sets the key identifying the group.
    /// </summary>
    public required string Key { get; set; }

    /// <summary>
    /// Gets the visibility of the separator based on the order of the group.
    /// </summary>
    public Visibility SeparatorVisibility => this.Order == 0 ? Visibility.Collapsed : Visibility.Visible;
}
