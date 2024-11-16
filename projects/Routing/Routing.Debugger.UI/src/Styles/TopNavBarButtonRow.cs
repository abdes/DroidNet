// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A styled <see cref="StackPanel" /> to be used for a row of buttons in the top navigation bar.
/// </summary>
public partial class TopNavBarButtonRow : StackPanel
{
    public TopNavBarButtonRow() => this.Style = (Style)Application.Current.Resources[nameof(TopNavBarButtonRow)];
}
