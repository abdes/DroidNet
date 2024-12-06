// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the view for displaying assets in a tiles layout in the World Editor.
/// </summary>
[ViewModel(typeof(TilesLayoutViewModel))]
public sealed partial class TilesLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TilesLayoutView"/> class.
    /// </summary>
    public TilesLayoutView()
    {
        this.InitializeComponent();
    }
}
