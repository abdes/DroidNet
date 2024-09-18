// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class GridForTreeItem : Grid
{
    public static readonly double CellPadding = 5;

    public static readonly double CellContentHeight = 24;
    public static readonly double CellContentWidth = 24;

    public GridForTreeItem()
    {
        this.RowDefinitions.Add(
            new RowDefinition()
            {
                Height = new GridLength(CellPadding + CellContentHeight + CellPadding),
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(CellPadding + CellContentWidth + CellPadding),
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(CellContentWidth),
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(1, GridUnitType.Star),
            });
    }
}
