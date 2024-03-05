// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Generators.Demo;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Generators;
using DroidNet.Routing.Generators.ViewModels;
using Microsoft.UI.Xaml;

/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
[ViewModel(typeof(DemoViewModel))]
[ExcludeFromCodeCoverage]
public partial class DemoView : DependencyObject;
