// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.TreeView.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class GridForTreeItem : Grid
{
    public GridForTreeItem()
    {
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = GridLength.Auto,
            });
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
    }
}
