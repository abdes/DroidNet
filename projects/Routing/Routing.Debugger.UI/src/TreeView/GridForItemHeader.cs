// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// A styled <see cref="Grid" /> for the items header.
/// </summary>
public partial class GridForItemHeader : Grid
{
    /// <summary>
    /// Initializes a new instance of the <see cref="GridForItemHeader"/> class.
    /// </summary>
    public GridForItemHeader()
    {
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = GridLength.Auto,
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(1, GridUnitType.Star),
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = GridLength.Auto,
            });
    }
}
