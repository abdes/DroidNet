// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Mvvm.Generators;

[ViewModel(typeof(TilesLayoutViewModel))]
public sealed partial class TilesLayoutView
{
    public TilesLayoutView() => this.InitializeComponent();
}
