// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A styled <see cref="TextBox" /> for the top navigation bar URL input.
/// </summary>
public partial class TopNavBarUrlBox : TextBox
{
    public TopNavBarUrlBox() => this.Style = (Style)Application.Current.Resources[nameof(TopNavBarUrlBox)];
}
