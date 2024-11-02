// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

[ViewModel(typeof(AssetsViewModel))]
public sealed partial class AssetsView : UserControl
{
    public AssetsView() => this.InitializeComponent();
}
