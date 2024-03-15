// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

internal sealed class PanelButtonRow : StackPanel
{
    public PanelButtonRow() => this.Style = (Style)Application.Current.Resources[nameof(PanelButtonRow)];
}
