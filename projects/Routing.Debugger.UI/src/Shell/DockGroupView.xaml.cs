// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using DroidNet.Routing.Generators;
using Microsoft.UI.Xaml.Controls;

[ViewModel(typeof(DockGroupViewModel))]
public sealed partial class DockGroupView : UserControl
{
    public DockGroupView() => this.InitializeComponent();
}
