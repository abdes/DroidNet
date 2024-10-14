// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using Microsoft.UI.Xaml;

/// <summary>
/// A styled <see cref="PanelButton" /> used for the minimize button of a dock panel.
/// </summary>
internal sealed partial class PanelMinimizeButton : PanelButton
{
    public PanelMinimizeButton() => this.Style = (Style)Application.Current.Resources[nameof(PanelMinimizeButton)];
}
