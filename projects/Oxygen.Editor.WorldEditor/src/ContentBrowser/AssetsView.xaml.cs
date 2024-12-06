// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the view for displaying assets in the World Editor.
/// </summary>
[ViewModel(typeof(AssetsViewModel))]
public sealed partial class AssetsView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetsView"/> class.
    /// </summary>
    public AssetsView()
    {
        this.InitializeComponent();
    }
}
