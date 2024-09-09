// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Views;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.ViewModels;

/// <summary>
/// The World Editor main view provides the primary UI for the user to create and manipulate world scenes and associated
/// entities.
/// </summary>
[ViewModel(typeof(MainViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class MainView : Page
{
    public MainView() => this.InitializeComponent();
}
