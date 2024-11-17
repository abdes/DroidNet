// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.Styles;

/// <summary>
/// A styled <see cref="TextBox" /> for the top navigation bar URL input.
/// </summary>
public partial class TopNavBarUrlBox : TextBox
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TopNavBarUrlBox"/> class.
    /// </summary>
    public TopNavBarUrlBox()
    {
        this.Style = (Style)Application.Current.Resources[nameof(TopNavBarUrlBox)];
    }
}
