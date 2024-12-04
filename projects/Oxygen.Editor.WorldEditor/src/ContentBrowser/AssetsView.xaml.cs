// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

[ViewModel(typeof(AssetsViewModel))]
public sealed partial class AssetsView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetsView"/> class.
    /// </summary>
    public AssetsView()
    {
        this.InitializeComponent();
    }
}
