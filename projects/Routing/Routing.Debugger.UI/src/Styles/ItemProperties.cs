// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A styled <see cref="StackPanel" /> for the properties of an item in the tree.
/// </summary>
public partial class ItemProperties : StackPanel
{
    public ItemProperties() => this.Style = (Style)Application.Current.Resources[nameof(ItemProperties)];
}
