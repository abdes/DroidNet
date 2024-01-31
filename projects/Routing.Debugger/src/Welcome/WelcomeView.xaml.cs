// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.Welcome;

using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Generators;

[ViewModel(typeof(ShellViewModel))]
public sealed partial class WelcomeView
{
    public WelcomeView() => this.InitializeComponent();
}
