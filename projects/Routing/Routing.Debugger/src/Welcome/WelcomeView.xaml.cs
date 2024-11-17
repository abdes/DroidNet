// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using DroidNet.Routing.Debugger.UI.Shell;

namespace DroidNet.Routing.Debugger.Welcome;

/// <summary>
/// A simple welcome page used as a placeholder for the application main view
/// inside the <seealso cref="ShellView" />.
/// </summary>
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
