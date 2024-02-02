// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using DroidNet.Routing.Generators;

/// <summary>
/// The view for the debugger shell.
/// </summary>
[ViewModel(typeof(ShellViewModel))]
public sealed partial class ShellView
{
    public ShellView() => this.InitializeComponent();
}
