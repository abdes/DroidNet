// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Docking.Demo.Controls;

/// <summary>A simple welcome page used as a placeholder for the main application content inside the docking workspace.</summary>
[ViewModel(typeof(WelcomeViewModel))]
public sealed partial class WelcomeView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WelcomeView"/> class.
    /// </summary>
    public WelcomeView()
    {
        this.InitializeComponent();
    }
}
