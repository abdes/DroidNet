// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class GridForItemHeader : Grid
{
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
