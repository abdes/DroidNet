// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using CommunityToolkit.WinUI.Controls;
using Microsoft.UI.Xaml;

public class DockingSplitter : GridSplitter
{
    public DockingSplitter() => this.Style = (Style)Application.Current.Resources[nameof(DockingSplitter)];

    public override string? ToString() => nameof(DockingSplitter);
}
