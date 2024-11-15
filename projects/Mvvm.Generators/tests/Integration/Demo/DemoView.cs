// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators.Tests.Demo;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
[ViewModel(typeof(DemoViewModel))]
[ExcludeFromCodeCoverage]
public partial class DemoView : DependencyObject;
