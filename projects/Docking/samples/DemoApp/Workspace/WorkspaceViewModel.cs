// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Workspace;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Layouts.GridFlow;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

/// <summary>The ViewModel for the docking workspace.</summary>
[InjectAs(ServiceLifetime.Transient)]
public partial class WorkspaceViewModel : ObservableObject
{
    [ObservableProperty]
    private UIElement? workspaceContent;

    public WorkspaceViewModel(IDocker docker, GridFlowLayout layout)
    {
        this.UpdateContent(docker, layout);

        docker.LayoutChanged += (_, args) =>
        {
            if (args.Reason is LayoutChangeReason.Docking)
            {
                this.UpdateContent(docker, layout);
            }
        };
    }

    private void UpdateContent(IDocker docker, GridFlowLayout layout)
    {
        docker.Layout(layout);
        var content = layout.CurrentGrid;
        this.WorkspaceContent = content as UIElement ??
                                throw new InvalidOperationException(
                                    "the provided layout engine does not produce a UIElement");
    }
}
