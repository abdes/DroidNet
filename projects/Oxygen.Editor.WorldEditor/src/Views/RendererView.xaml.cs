// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Views;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.ViewModels;

[ViewModel(typeof(RendererViewModel))]
[InjectAs(ServiceLifetime.Transient)]
public sealed partial class RendererView : UserControl
{
    public RendererView() => this.InitializeComponent();
}
