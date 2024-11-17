// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.Styles;

/// <summary>
/// A styled <see cref="StackPanel" /> to be used for a row of buttons in the top navigation bar.
/// </summary>
public partial class TopNavBarButtonRow : StackPanel
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TopNavBarButtonRow"/> class.
    /// </summary>
    public TopNavBarButtonRow()
    {
        this.Style = (Style)Application.Current.Resources[nameof(TopNavBarButtonRow)];
    }
}
