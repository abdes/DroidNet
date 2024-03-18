// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Workspace;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

[InjectAs(ServiceLifetime.Transient)]
public partial class WorkspaceViewModel : ObservableObject
{
    [ObservableProperty]
    private UIElement? workspaceContent;

    public WorkspaceViewModel(IDocker docker, LayoutEngine layout)
    {
        this.UpdateContent(docker, layout);

        docker.LayoutChanged += reason =>
        {
            if (reason is LayoutChangeReason.Docking or LayoutChangeReason.Resize)
            {
                this.UpdateContent(docker, layout);
            }
        };
    }

    private void UpdateContent(IDocker docker, LayoutEngine layout)
    {
        var content = layout.Build(docker.Root);
        this.WorkspaceContent = content as UIElement ??
                                throw new InvalidOperationException(
                                    "the provided layout engine does not produce a UIElement");
    }
}
