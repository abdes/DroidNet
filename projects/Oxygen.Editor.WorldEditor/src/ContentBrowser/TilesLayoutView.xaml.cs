// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml.Controls;

[ViewModel(typeof(TilesLayoutViewModel))]
[InjectAs(ServiceLifetime.Transient)]
public sealed partial class TilesLayoutView : UserControl
{
    public TilesLayoutView() => this.InitializeComponent();
}
