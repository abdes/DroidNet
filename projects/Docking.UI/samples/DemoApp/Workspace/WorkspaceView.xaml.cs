// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Workspace;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;

[ViewModel(typeof(WorkspaceViewModel))]
[InjectAs(ServiceLifetime.Transient)]
public sealed partial class WorkspaceView
{
    public WorkspaceView() => this.InitializeComponent();
}
