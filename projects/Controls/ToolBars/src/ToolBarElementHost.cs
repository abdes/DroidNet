// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Hosts a toolbar element as content.
/// </summary>
public partial class ToolBarElementHost : ContentControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ToolBarElementHost"/> class.
    /// </summary>
    public ToolBarElementHost()
    {
        this.DefaultStyleKey = typeof(ToolBarElementHost);
    }
}
