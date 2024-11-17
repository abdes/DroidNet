// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI.Controls;
using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Controls;

/// <summary>
/// A <see cref="GridSplitter" /> custom styled for separating docks.
/// </summary>
public partial class DockingSplitter : GridSplitter
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DockingSplitter"/> class.
    /// </summary>
    public DockingSplitter()
    {
        this.Style = (Style)Application.Current.Resources[nameof(DockingSplitter)];
    }

    /// <inheritdoc/>
    public override string ToString() => nameof(DockingSplitter);
}
