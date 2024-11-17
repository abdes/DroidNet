// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.Styles;

/// <summary>
/// A styled <see cref="StackPanel" /> for the properties of an item in the tree.
/// </summary>
public partial class ItemProperties : StackPanel
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ItemProperties"/> class.
    /// </summary>
    public ItemProperties()
    {
        this.Style = (Style)Application.Current.Resources[nameof(ItemProperties)];
    }
}
