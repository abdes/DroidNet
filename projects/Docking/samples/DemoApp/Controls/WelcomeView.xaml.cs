// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Controls;

using DroidNet.Mvvm.Generators;

/// <summary>A simple welcome page used as a placeholder for the main application content inside the docking workspace.</summary>
[ViewModel(typeof(WelcomeViewModel))]
public sealed partial class WelcomeView
{
    public WelcomeView() => this.InitializeComponent();
}
