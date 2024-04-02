// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Shell;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Demo.Workspace;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Hosting.Generators;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;

/// <summary>The ViewModel for the application main window shell.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class ShellViewModel : ObservableObject
{
    private readonly IResolver resolver;

    [ObservableProperty]
    private WorkspaceViewModel workspace;

    public ShellViewModel(IResolver resolver, IDocker docker)
    {
        this.resolver = resolver;
        this.Workspace = this.CreateWorkspace(docker);
    }

    private WorkspaceViewModel CreateWorkspace(IDocker docker)
    {
        var dockViewFactory = this.resolver.Resolve<IDockViewFactory>();
        var layout = new GridFlowLayout(dockViewFactory);
        return this.resolver.Resolve<Func<IDocker, LayoutEngine, WorkspaceViewModel>>()(docker, layout);
    }
}
