// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using Microsoft.UI.Xaml;

internal sealed class PanelDockButton : PanelButton
{
    public PanelDockButton() => this.Style = (Style)Application.Current.Resources[nameof(PanelDockButton)];
}
