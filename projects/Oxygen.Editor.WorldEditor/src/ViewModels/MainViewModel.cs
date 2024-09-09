// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// The view model for the World Editor main view.
/// </summary>
[InjectAs(ServiceLifetime.Singleton)]
public class MainViewModel : ObservableObject;
